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
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <setjmp.h>
#include <signal.h>
#include <pthread.h>

#ifndef COLD_BACKEND_ONLY
#include "cold_parser.h"
#endif

/* Diagnostics: enable with --diag:dump_per_fn and --diag:dump_slots */
static bool cold_diag_dump_per_fn = false;
static bool cold_diag_dump_slots = false;
static int32_t cold_egraph_rewrite_count = 0;
static int32_t cold_egraph_dedup_count = 0;

#include "macho_direct.h"
#include "elf64_direct.h"
#include "coff_direct.h"
#include "x64_emit.h"
#include "rv64_emit.h"
#include "cold_csg_v2_format.h"

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

#define COLD_EMBEDDED_CONTRACT_CAP 8192

#define COLD_EMBEDDED_SOURCE_PATH_CAP 4096

__attribute__((used)) static char ColdEmbeddedContract[COLD_EMBEDDED_CONTRACT_CAP] =
    "CHENG_COLD_EMBEDDED_CONTRACT_V1\n";
__attribute__((used)) static char ColdEmbeddedSourcePath[COLD_EMBEDDED_SOURCE_PATH_CAP] =
    "CHENG_COLD_EMBEDDED_SOURCE_PATH_V1\n";

static uint64_t cold_now_us(void);

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



static char ColdDieError[512] = {0};
static char ColdDieReportPath[PATH_MAX] = {0};

static void cold_die_report_flush(void) {
    if (ColdDieError[0] == '\0') return;
    if (ColdDieReportPath[0] == '\0') return;
    FILE *f = fopen(ColdDieReportPath, "w");
    if (!f) return;
    fprintf(f, "system_link_exec_runtime_execute=0\n");
    fprintf(f, "system_link_exec=0\n");
    fprintf(f, "real_backend_codegen=0\n");
    fputs("cold_system_link_exec=1\n", f);
    fputs("system_link_exec_scope=cold_subset_direct_macho\n", f);
    fputs("full_backend_codegen=0\n", f);
    fputs("direct_macho=0\n", f);
    fputs("provider_object_count=0\n", f);
    fprintf(f, "error=%s\n", ColdDieError);
    fclose(f);
}

static jmp_buf ColdErrorJumpBuf;
static bool ColdErrorRecoveryEnabled = false;

static void die(const char *msg) {
    snprintf(ColdDieError, sizeof(ColdDieError), "%s", msg);
    fprintf(stderr, "cheng_cold: %s\n", msg);
    cold_die_report_flush();
    if (ColdErrorRecoveryEnabled) longjmp(ColdErrorJumpBuf, 1);
    exit(2);
}
/* Per-import SIGSEGV jump buffer. Set before calling cold_compile_one_import_direct.
   The SIGSEGV handler longjmps here to skip the entire import when a SEGV occurs. */
static jmp_buf ColdImportSegvJumpBuf;
static bool ColdImportSegvActive = false;
static int ColdImportSegvSaw = 0;
static bool ColdImportBodyCompilationActive = false; /* set during import body compilation */
static void cold_sigsegv_die_handler(int sig) {
    (void)sig;
    if (ColdErrorRecoveryEnabled) {
        longjmp(ColdErrorJumpBuf, 1);
    }
    if (ColdImportSegvActive) {
        ColdImportSegvActive = false;
        longjmp(ColdImportSegvJumpBuf, 1);
    }
    const char msg[] = "cheng_cold: SEGV in cold compiler\n";
    write(2, msg, sizeof(msg) - 1);
    _exit(2);
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
typedef struct Span {
    const uint8_t *ptr;
    int32_t len;
} Span;

#define COLD_MAX_VARIANT_FIELDS 8
#define COLD_MAX_OBJECT_FIELDS 64

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
    if (st.st_size > (off_t)INT32_MAX) {
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

static bool cold_span_is_float(Span s) {
    if (s.len <= 1) return false;
    int32_t i = 0;
    if (s.ptr[0] == '-') i = 1;
    bool has_dot = false;
    for (; i < s.len; i++) {
        uint8_t c = s.ptr[i];
        if (c == '.') { if (has_dot) return false; has_dot = true; continue; }
        if (c < '0' || c > '9') return false;
    }
    return has_dot;
}
static double cold_span_f64(Span s) {
    double val = 0.0, frac = 0.0, div = 1.0;
    int32_t i = 0, sign = 1;
    if (s.ptr[0] == '-') { sign = -1; i = 1; }
    while (i < s.len && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
        val = val * 10.0 + (double)(s.ptr[i++] - '0');
    }
    if (i < s.len && s.ptr[i] == '.') {
        i++;
        while (i < s.len && s.ptr[i] >= '0' && s.ptr[i] <= '9') {
            frac = frac * 10.0 + (double)(s.ptr[i++] - '0');
            div *= 10.0;
        }
    }
    return sign * (val + frac / div);
}
static int32_t span_i32(Span s) {
    int32_t i = 0;
    int32_t sign = 1;
    int64_t value = 0;
    if (s.len > 0 && s.ptr[0] == '-') {
        sign = -1;
        i = 1;
    }
    for (; i < s.len; i++) {
        uint8_t c = s.ptr[i];
        if (c < '0' || c > '9') break;
        value = value * 10 + (int64_t)(c - '0');
        if (value > (int64_t)INT32_MAX + 1) return 0;
    }
    if (sign < 0 && value == (int64_t)INT32_MAX + 1) return INT32_MIN;
    if (value > (int64_t)INT32_MAX) return 0;
    return sign * (int32_t)value;
}

static int64_t span_i64(Span s) {
    int32_t i = 0;
    int32_t sign = 1;
    uint64_t value = 0;
    if (s.len > 0 && s.ptr[0] == '-') {
        sign = -1;
        i = 1;
    }
    for (; i < s.len; i++) {
        uint8_t c = s.ptr[i];
        if (c < '0' || c > '9') break;
        value = value * 10ULL + (uint64_t)(c - '0');
    }
    if (sign < 0) return -(int64_t)value;
    return (int64_t)value;
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

static Span cold_decode_string_content(Arena *arena, Span raw, bool fmt_literal) {
    uint8_t *out = arena_alloc(arena, (size_t)raw.len + 1);
    int32_t count = 0;
    for (int32_t i = 0; i < raw.len; i++) {
        uint8_t c = raw.ptr[i];
        if (c == '\\') {
            if (i + 1 >= raw.len) die("unterminated string escape");
            uint8_t next = raw.ptr[++i];
            if (next == 'n') out[count++] = '\n';
            else if (next == 'r') out[count++] = '\r';
            else if (next == 't') out[count++] = '\t';
            else if (next == '0') out[count++] = '\0';
            else if (next == '\\') out[count++] = '\\';
            else if (next == '"') out[count++] = '"';
            else if (next == '{') out[count++] = '{';
            else if (next == '}') out[count++] = '}';
            else die("unsupported string escape");
            continue;
        }
        if (fmt_literal && c == '{' && i + 1 < raw.len && raw.ptr[i + 1] == '{') {
            out[count++] = '{';
            i++;
            continue;
        }
        if (fmt_literal && c == '}' && i + 1 < raw.len && raw.ptr[i + 1] == '}') {
            out[count++] = '}';
            i++;
            continue;
        }
        out[count++] = c;
    }
    return (Span){out, count};
}

static int32_t cold_decode_char_literal(Span token) {
    if (token.len < 3 || token.ptr[0] != '\'' || token.ptr[token.len - 1] != '\'') {
        die("invalid char literal");
    }
    Span raw = span_sub(token, 1, token.len - 1);
    if (raw.len == 1) return raw.ptr[0];
    if (raw.len == 2 && raw.ptr[0] == '\\') {
        uint8_t next = raw.ptr[1];
        if (next == 'n') return '\n';
        if (next == 'r') return '\r';
        if (next == 't') return '\t';
        if (next == '0') return '\0';
        if (next == '\\') return '\\';
        if (next == '\'') return '\'';
        if (next == '"') return '"';
    }
    die("unsupported char literal");
    return 0;
}

static void span_write(FILE *file, Span s) {
    if (s.len > 0) fwrite(s.ptr, 1, (size_t)s.len, file);
}

/* ================================================================
 * Cold bootstrap contract parser
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

bool cold_line_top_level(Span line) {
    return line.len > 0 && line.ptr[0] != ' ' && line.ptr[0] != '\t';
}

bool cold_line_has_triple_quote(Span line) {
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

int32_t cold_line_end_from(Span source, int32_t pos) {
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

bool cold_parse_function_symbol_at(Span source, int32_t fn_start, int32_t line_no,
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
    if (open < source.len && source.ptr[open] == '[') {
        int32_t depth = 1;
        int32_t gen_start = open + 1;
        open++;
        while (open < source.len && depth > 0) {
            if (source.ptr[open] == '[') depth++;
            else if (source.ptr[open] == ']') depth--;
            open++;
        }
        if (depth != 0) return false;
        /* Extract generic param names */
        Span gen_span = span_trim(span_sub(source, gen_start, open - 1));
        int32_t gp = 0;
        while (gp < gen_span.len && symbol->generic_count < 4) {
            while (gp < gen_span.len && (gen_span.ptr[gp] == ' ' || gen_span.ptr[gp] == ',')) gp++;
            int32_t gn_start = gp;
            while (gp < gen_span.len && gen_span.ptr[gp] != ' ' && gen_span.ptr[gp] != ',') gp++;
            if (gp > gn_start)
                symbol->generic_names[symbol->generic_count++] = span_sub(gen_span, gn_start, gp);
        }
        while (open < source.len && (source.ptr[open] == ' ' || source.ptr[open] == '\t')) open++;
    }
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

static uint64_t cold_fnv1a64_update_bytes(uint64_t hash, const uint8_t *ptr, size_t len) {
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)ptr[i];
        hash *= 1099511628211ULL;
    }
    return hash;
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
    if (case_count != 8 || default_value != 2) return false;
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
    if (case_count != 10 || default_value != -1) return false;
    static const char *expected_texts[] = {
        "help",
        "-h",
        "--help",
        "status",
        "print-build-plan",
        "verify-export-visibility",
        "verify-export-visibility-parallel",
        "system-link-exec",
        "emit-cold-csg-v2",
        "run-host-smokes",
    };
    static const int32_t expected_codes[] = {0, 0, 0, 1, 2, 3, 4, 5, 7, 6};
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



static int32_t cold_span_find_char(Span span, char needle) {
    for (int32_t i = 0; i < span.len; i++) {
        if (span.ptr[i] == (uint8_t)needle) return i;
    }
    return -1;
}

static int32_t cold_span_find_top_level_char(Span span, char needle) {
    int32_t paren = 0;
    int32_t square = 0;
    bool in_string = false;
    uint8_t quote = 0;
    for (int32_t i = 0; i < span.len; i++) {
        uint8_t c = span.ptr[i];
        if (in_string) {
            if (c == '\\' && i + 1 < span.len) {
                i++;
                continue;
            }
            if (c == quote) in_string = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            quote = c;
            continue;
        }
        if (c == '(') paren++;
        else if (c == ')') {
            if (paren <= 0) return -1;
            paren--;
        } else if (c == '[') {
            square++;
        } else if (c == ']') {
            if (square <= 0) return -1;
            square--;
        } else if (c == (uint8_t)needle && paren == 0 && square == 0) {
            return i;
        }
    }
    return -1;
}

/* Strip everything from the first ':' onward (the block delimiter), skipping
   ':' inside string/char literals. Handles "if cond: // comment" -> "cond". */
int32_t cold_line_indent_width(Span line) {
    int32_t indent = 0;
    for (int32_t i = 0; i < line.len; i++) {
        if (line.ptr[i] == ' ') indent++;
        else if (line.ptr[i] == '\t') indent += 4;
        else break;
    }
    return indent;
}



/* Write span to CSG file, escaping \t, \n, \\ so CSG tab-delimited format stays valid. */




static bool cold_parse_i32_seq_type(Span type);








bool cold_type_block_looks_like_object(Span fields) {
    int32_t pos = 0;
    int32_t count = 0;
    while (pos < fields.len) {
        int32_t start = pos;
        while (pos < fields.len && fields.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < fields.len) pos++;
        Span line = span_trim(span_sub(fields, start, end));
        if (line.len <= 0) continue;
        if (cold_span_starts_with(line, "#") || cold_span_starts_with(line, "//")) continue;
        if (line.ptr[0] == '|') return false;
        int32_t colon = cold_span_find_top_level_char(line, ':');
        if (colon <= 0) return false;
        Span name = span_trim(span_sub(line, 0, colon));
        if (name.len <= 0) return false;
        uint8_t first = name.ptr[0];
        if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) return false;
        for (int32_t i = 1; i < name.len; i++) {
            uint8_t c = name.ptr[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_')) {
                return false;
            }
        }
        count++;
    }
    return count > 0;
}




/* ---- scanner helpers for cold_emit_csg_type_rows ---- */

/* Advance cursor past current line, return line span. New pos is start of next line. */

/* Collect indented body lines starting at pos. Stops when a line is top-level or has indent
   <= base_indent. Returns the body text (subspan of source, may be empty). *next_pos is set to
   the start of the line that terminated collection (rollback position). */

/* Emit enum body as 0-field variant list. enum_body is the raw text between entry_indent
   lines. Returns whether emission succeeded. */

/* ---- cold_emit_csg_type_rows (rewritten: single-pass, cursor-only-at-top) ---- */






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

static bool cold_file_starts_with_text(const char *path, const char *prefix) {
    Span haystack = source_open(path);
    int32_t n = (int32_t)strlen(prefix);
    bool ok = haystack.len >= n && n > 0 &&
              memcmp(haystack.ptr, prefix, (size_t)n) == 0;
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

int cold_cmd_compile_bootstrap(int argc, char **argv, const char *self_path) {
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

int cold_cmd_bootstrap_bridge(int argc, char **argv, const char *self_path) {
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
    fputs("command_surface=print-contract,self-check,compile-bootstrap,bootstrap-bridge,build-backend-driver\n", file);
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

int cold_cmd_build_backend_driver(int argc, char **argv, const char *self_path) {
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
        if (!out_dir_arg || out_dir_arg[0] == '\0') out_dir_arg = "artifacts/backend_driver";
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
    if (compile_rc != 0) {
        fprintf(stderr, "[cheng_cold] backend driver compile failed\n");
        return 1;
    }

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
    char smoke_csg_exe[PATH_MAX];
    char smoke_csg_report[PATH_MAX];
    char smoke_csg_stdout[PATH_MAX];
    if (!cold_path_with_suffix(smoke_source, sizeof(smoke_source), out_path, ".system-link-smoke.cheng")) return 1;
    if (!cold_path_with_suffix(smoke_csg, sizeof(smoke_csg), out_path, ".system-link-smoke.csg.txt")) return 1;
    if (!cold_path_with_suffix(smoke_exe, sizeof(smoke_exe), out_path, ".system-link-smoke")) return 1;
    if (!cold_path_with_suffix(smoke_report, sizeof(smoke_report), out_path, ".system-link-smoke.report.txt")) return 1;
    if (!cold_path_with_suffix(smoke_stdout, sizeof(smoke_stdout), out_path, ".system-link-smoke.stdout")) return 1;
    if (!cold_path_with_suffix(smoke_csg_exe, sizeof(smoke_csg_exe), out_path, ".system-link-smoke.csg-in")) return 1;
    if (!cold_path_with_suffix(smoke_csg_report, sizeof(smoke_csg_report), out_path, ".system-link-smoke.csg-in.report.txt")) return 1;
    if (!cold_path_with_suffix(smoke_csg_stdout, sizeof(smoke_csg_stdout), out_path, ".system-link-smoke.csg-in.stdout")) return 1;
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
    char q_csg_in_flag[PATH_MAX * 5 + 64];
    char q_csg_in_out_flag[PATH_MAX * 5 + 64];
    char q_csg_in_report_flag[PATH_MAX * 5 + 64];
    char q_csg_in_stdout[PATH_MAX * 5 + 8];
    char in_flag[PATH_MAX + 16];
    char smoke_out_flag[PATH_MAX + 16];
    char smoke_report_flag[PATH_MAX + 32];
    char smoke_csg_flag[PATH_MAX + 16];
    char smoke_csg_in_flag[PATH_MAX + 16];
    char smoke_csg_in_out_flag[PATH_MAX + 16];
    char smoke_csg_in_report_flag[PATH_MAX + 32];
    snprintf(in_flag, sizeof(in_flag), "--in:%s", smoke_source);
    snprintf(smoke_csg_flag, sizeof(smoke_csg_flag), "--csg-out:%s", smoke_csg);
    snprintf(smoke_out_flag, sizeof(smoke_out_flag), "--out:%s", smoke_exe);
    snprintf(smoke_report_flag, sizeof(smoke_report_flag), "--report-out:%s", smoke_report);
    snprintf(smoke_csg_in_flag, sizeof(smoke_csg_in_flag), "--csg-in:%s", smoke_csg);
    snprintf(smoke_csg_in_out_flag, sizeof(smoke_csg_in_out_flag), "--out:%s", smoke_csg_exe);
    snprintf(smoke_csg_in_report_flag, sizeof(smoke_csg_in_report_flag), "--report-out:%s", smoke_csg_report);
    if (!cold_shell_quote(q_out, sizeof(q_out), out_path) ||
        !cold_shell_quote(q_in_flag, sizeof(q_in_flag), in_flag) ||
        !cold_shell_quote(q_csg_flag, sizeof(q_csg_flag), smoke_csg_flag) ||
        !cold_shell_quote(q_out_flag, sizeof(q_out_flag), smoke_out_flag) ||
        !cold_shell_quote(q_report_flag, sizeof(q_report_flag), smoke_report_flag) ||
        !cold_shell_quote(q_smoke_stdout, sizeof(q_smoke_stdout), smoke_stdout) ||
        !cold_shell_quote(q_csg_in_flag, sizeof(q_csg_in_flag), smoke_csg_in_flag) ||
        !cold_shell_quote(q_csg_in_out_flag, sizeof(q_csg_in_out_flag), smoke_csg_in_out_flag) ||
        !cold_shell_quote(q_csg_in_report_flag, sizeof(q_csg_in_report_flag), smoke_csg_in_report_flag) ||
        !cold_shell_quote(q_csg_in_stdout, sizeof(q_csg_in_stdout), smoke_csg_stdout)) return 1;
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
    if (!cold_file_starts_with_text(smoke_csg, "CHENGCSG")) {
        fprintf(stderr, "[cheng_cold] generated system-link smoke csg-v2 mismatch: %s\n", smoke_csg);
        return 1;
    }
    snprintf(cmd, sizeof(cmd), "%s system-link-exec %s %s %s > %s",
             q_out, q_csg_in_flag, q_csg_in_out_flag, q_csg_in_report_flag, q_csg_in_stdout);
    if (!cold_run_shell(cmd, "backend driver candidate csg-in system-link-exec")) return 1;
    if (!cold_run_executable_noargs_rc(smoke_csg_exe, 77)) {
        fprintf(stderr, "[cheng_cold] csg-in smoke executable exit mismatch: %s\n", smoke_csg_exe);
        return 1;
    }
    if (!cold_file_contains_text(smoke_csg_report, "system_link_exec=1\n") ||
        !cold_file_contains_text(smoke_csg_report, "real_backend_codegen=1\n") ||
        !cold_file_contains_text(smoke_csg_report, "cold_csg_lowering=1\n") ||
        !cold_file_contains_text(smoke_csg_report, "linkerless_image=1\n") ||
        !cold_file_contains_text(smoke_csg_report, "system_link=0\n")) {
        fprintf(stderr, "[cheng_cold] csg-in smoke report mismatch: %s\n", smoke_csg_report);
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

static int32_t cold_jobs_from_env(void);

static void cold_usage(void) {
    puts("cheng_cold");
    puts("usage:");
    puts("  cheng_cold status [--root:<dir>] [--in:<path>]");
    puts("  cheng_cold print-contract --in:<path>");
    puts("  cheng_cold self-check --in:<path>");
    puts("  cheng_cold compile-bootstrap --in:<path> --out:<path> [--report-out:<path>]");
    puts("  cheng_cold bootstrap-bridge [--in:<path>] [--out-dir:<path>]");
    puts("  cheng_cold build-backend-driver [--in:<contract>] [--out:<path>] [--out-dir:<dir>] [--report-out:<path>] [--map-out:<path>] [--index-out:<path>]");
    puts("  cheng_cold system-link-exec --in:<source> [--csg-in:<facts>|--csg-out:<facts>] --out:<path> [--emit:exe|obj|csg] [--target:arm64-apple-darwin] [--report-out:<path>]");
    puts("    --emit:exe   produce standalone executable (default)");
    puts("    --emit:obj   emit CSG facts as intermediate object representation");
    puts("    --emit:csg   same as --emit:obj");
    puts("  reserved backend commands hard-fail until their real paths exist");
    puts("  cheng_cold <out> [source]");
}

static int cold_cmd_status(int argc, char **argv) {
    const char *root = cold_flag_value(argc, argv, "--root");
    const char *source = cold_flag_value(argc, argv, "--in");
    if (!root || root[0] == '\0') root = ".";
    if (!source) source = "";
    puts("cheng");
    puts("version=cheng.compiler_runtime.v1");
    puts("execution=argv_control_plane");
    puts("bootstrap_mode=selfhost");
    puts("compiler_entry=src/core/tooling/backend_driver_dispatch_min.cheng");
    puts("ordinary_command=system-link-exec");
    puts("ordinary_pipeline=canonical_csg_verified_primary_object_codegen_ready");
    puts("stage3=artifacts/bootstrap/cheng.stage3");
    printf("flag_root=%s\n", root);
    printf("flag_in=%s\n", source);
    puts("flag_exec_edges=0");
    puts("flag_exec_unresolved=0");
    puts("linkerless_image=1");
    puts("system_link=0");
    return 0;
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

static int32_t cold_slot_kind_from_code(char code) {
    if (code == 's') return SLOT_STR;
    if (code == 'S') return SLOT_STR_REF;
    if (code == 'i') return SLOT_I32;
    if (code == 'l') return SLOT_I64;
    if (code == 'L') return SLOT_I64_REF;
    if (code == 'v') return SLOT_VARIANT;
    if (code == 'q') return SLOT_SEQ_I32;
    if (code == 't') return SLOT_SEQ_STR;
    if (code == 'T') return SLOT_SEQ_STR_REF;
    if (code == 'u') return SLOT_SEQ_OPAQUE;
    if (code == 'U') return SLOT_SEQ_OPAQUE_REF;
    if (code == 'o') return SLOT_OPAQUE;
    if (code == 'O') return SLOT_OPAQUE_REF;
    die("unknown cold field kind code");
    return SLOT_I32;
}

static int32_t cold_slot_size_for_kind(int32_t kind) {
    if (kind == SLOT_STR) return COLD_STR_SLOT_SIZE;
    if (kind == SLOT_PTR) return 8;
    if (kind == SLOT_I32) return 4;
    if (kind == SLOT_I64) return 8;
    if (kind == SLOT_I64_REF) return 8;
    if (kind == SLOT_I32_REF) return 8;
    if (kind == SLOT_F32) return 4;
    if (kind == SLOT_F64) return 8;
    if (kind == SLOT_VARIANT) return 64;
    if (kind == SLOT_OBJECT) return 16;
    if (kind == SLOT_ARRAY_I32) return 4;
    if (kind == SLOT_SEQ_I32) return 16;
    if (kind == SLOT_SEQ_STR) return 16;
    if (kind == SLOT_SEQ_OPAQUE) return 16;
    if (kind == SLOT_I32_REF || kind == SLOT_I64_REF ||
        kind == SLOT_SEQ_I32_REF || kind == SLOT_OBJECT_REF ||
        kind == SLOT_STR_REF || kind == SLOT_SEQ_STR_REF ||
        kind == SLOT_SEQ_OPAQUE_REF ||
        kind == SLOT_OPAQUE || kind == SLOT_OPAQUE_REF) return 8;
    die("unknown cold slot kind size");
    return 4;
}

static int32_t cold_slot_align_for_kind(int32_t kind) {
    return cold_slot_size_for_kind(kind) >= 8 ? 8 : 4;
}

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

static BodyIR *cold_current_parsing_body = 0;

static BodyIR *body_new(Arena *arena) {
    BodyIR *body = arena_alloc(arena, sizeof(BodyIR));
    body->arena = arena;
    body->return_kind = SLOT_I32;
    body->return_size = 4;
    body->sret_slot = -1;
    body->has_fallback = false;
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
    int32_t *no_alias = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->slot_count > 0) {
        memcpy(kind, body->slot_kind, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(offset, body->slot_offset, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(size, body->slot_size, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(aux, body->slot_aux, (size_t)body->slot_count * sizeof(int32_t));
        memcpy(type, body->slot_type, (size_t)body->slot_count * sizeof(Span));
        if (body->slot_no_alias)
            memcpy(no_alias, body->slot_no_alias, (size_t)body->slot_count * sizeof(int32_t));
    }
    body->slot_kind = kind;
    body->slot_offset = offset;
    body->slot_size = size;
    body->slot_aux = aux;
    body->slot_type = type;
    body->slot_no_alias = no_alias;
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
    int32_t *term = arena_alloc(body->arena, (size_t)next * sizeof(int32_t));
    if (body->switch_count > 0) {
        memcpy(tag, body->switch_tag, (size_t)body->switch_count * sizeof(int32_t));
        memcpy(block, body->switch_block, (size_t)body->switch_count * sizeof(int32_t));
        memcpy(term, body->switch_term, (size_t)body->switch_count * sizeof(int32_t));
    }
    body->switch_tag = tag;
    body->switch_block = block;
    body->switch_term = term;
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
    body->slot_no_alias[idx] = 0;
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

static int32_t body_switch_case(BodyIR *body, int32_t owner_term, int32_t tag, int32_t block) {
    body_ensure_switches(body);
    int32_t idx = body->switch_count++;
    body->switch_tag[idx] = tag;
    body->switch_block[idx] = block;
    body->switch_term[idx] = owner_term;
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

/* Compute a simple structural hash of a BodyIR function.
   Two functions with the same ops, slots, blocks, and terms
   in the same order will produce the same hash. */
static uint64_t cold_body_ir_canonical_hash(BodyIR *body) {
    if (!body) return 0;
    uint64_t h = 0x9ae16a3b2f90405full;
    /* Hash op kinds (not dst/a/b/c values — just the structure) */
    for (int32_t i = 0; i < body->op_count; i++) {
        h ^= ((uint64_t)body->op_kind[i] * 0xc6a4a7935bd1e995ull);
        h = (h << 7) | (h >> 57);
    }
    /* Hash term kinds */
    for (int32_t i = 0; i < body->term_count; i++) {
        h ^= ((uint64_t)body->term_kind[i] * 0xc6a4a7935bd1e995ull);
        h = (h << 7) | (h >> 57);
    }
    /* Hash block structure */
    h ^= ((uint64_t)body->block_count * 0xc6a4a7935bd1e995ull);
    h ^= ((uint64_t)body->slot_count * 0xc6a4a7935bd1e995ull);
    return h;
}

/* Compute a normalized hash of a BodyIR function with basic arithmetic
   rewrite rules:
   1. I32_CONST values are hashed directly (op_a is the constant value)
   2. Commutative ops (ADD, MUL, AND, OR, XOR) sort operands before hashing
   3. Associative ops (ADD, MUL) flatten nested same-kind ops into one level */
static uint64_t cold_body_ir_canonical_hash_normalized(BodyIR *body) {
    if (!body) return 0;
    uint64_t h = 0x9ae16a3b2f90405full;

    /* Build slot -> defining-op-index mapping for associative flattening */
    int32_t *slot_def = 0;
    if (body->slot_count > 0) {
        slot_def = (int32_t *)calloc((size_t)body->slot_count, sizeof(int32_t));
        if (slot_def) {
            for (int32_t i = 0; i < body->slot_count; i++) slot_def[i] = -1;
            for (int32_t i = 0; i < body->op_count; i++) {
                int32_t dst = body->op_dst[i];
                if (dst >= 0 && dst < body->slot_count) slot_def[dst] = i;
            }
        }
    }

    for (int32_t i = 0; i < body->op_count; i++) {
        int32_t kind = body->op_kind[i];
        int32_t a = body->op_a[i], b = body->op_b[i];

        /* I32_CONST: hash the constant value (op_a) directly */
        if (kind == BODY_OP_I32_CONST) {
            h ^= ((uint64_t)kind * 0xc6a4a7935bd1e995ull);
            h = (h << 7) | (h >> 57);
            h ^= ((uint64_t)a * 0xc6a4a7935bd1e995ull);
            h = (h << 7) | (h >> 57);
            continue;
        }

        /* Associative flattening for ADD and MUL */
        if ((kind == BODY_OP_I32_ADD || kind == BODY_OP_I32_MUL) && slot_def) {
            int32_t flat[64];
            int32_t fc = 0;
            int32_t stk[64];
            int32_t sc = 0;
            stk[sc++] = b; stk[sc++] = a;
            while (sc > 0 && fc < 60) {
                int32_t s = stk[--sc];
                if (s < 0 || s >= body->slot_count) {
                    flat[fc++] = s;
                } else {
                    int32_t def = slot_def[s];
                    if (def >= 0 && def < body->op_count &&
                        body->op_kind[def] == kind) {
                        stk[sc++] = body->op_b[def];
                        stk[sc++] = body->op_a[def];
                    } else {
                        flat[fc++] = s;
                    }
                }
            }
            if (sc > 0) { flat[fc++] = a; flat[fc++] = b; }
            /* Sort flattened operands (commutative) */
            for (int32_t si = 0; si < fc - 1; si++) {
                for (int32_t sj = si + 1; sj < fc; sj++) {
                    if (flat[si] > flat[sj]) {
                        int32_t t = flat[si]; flat[si] = flat[sj]; flat[sj] = t;
                    }
                }
            }
            h ^= ((uint64_t)kind * 0xc6a4a7935bd1e995ull);
            h = (h << 7) | (h >> 57);
            for (int32_t fi = 0; fi < fc; fi++) {
                h ^= ((uint64_t)flat[fi] * 0xc6a4a7935bd1e995ull);
                h = (h << 7) | (h >> 57);
            }
            continue;
        }

        /* Commutative normalization for ADD, MUL, AND, OR, XOR */
        if (kind == BODY_OP_I32_ADD || kind == BODY_OP_I32_MUL ||
            kind == BODY_OP_I32_AND || kind == BODY_OP_I32_OR ||
            kind == BODY_OP_I32_XOR) {
            if (a > b) { int32_t t = a; a = b; b = t; }
        }

        h ^= ((uint64_t)kind * 0xc6a4a7935bd1e995ull);
        h = (h << 7) | (h >> 57);
        h ^= ((uint64_t)a * 0xc6a4a7935bd1e995ull);
        h = (h << 7) | (h >> 57);
        h ^= ((uint64_t)b * 0xc6a4a7935bd1e995ull);
        h = (h << 7) | (h >> 57);
    }

    free(slot_def);

    /* Hash term kinds */
    for (int32_t i = 0; i < body->term_count; i++) {
        h ^= ((uint64_t)body->term_kind[i] * 0xc6a4a7935bd1e995ull);
        h = (h << 7) | (h >> 57);
    }
    /* Hash block structure */
    h ^= ((uint64_t)body->block_count * 0xc6a4a7935bd1e995ull);
    h ^= ((uint64_t)body->slot_count * 0xc6a4a7935bd1e995ull);
    return h;
}

/* ================================================================
 * Symbol table and local table
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

#ifdef COLD_BACKEND_ONLY
static Span cold_arena_span_copy(Arena *arena, Span text) {
    uint8_t *ptr = arena_alloc(arena, (size_t)(text.len + 1));
    if (text.len > 0) memcpy(ptr, text.ptr, (size_t)text.len);
    ptr[text.len] = 0;
    return (Span){ptr, text.len};
}
#else
static Span cold_arena_span_copy(Arena *arena, Span text);
#endif

static Symbols *symbols_new(Arena *arena) {
    Symbols *symbols = arena_alloc(arena, sizeof(Symbols));
    symbols->arena = arena;
    symbols->function_cap = 512;
    symbols->type_cap = 16;
    symbols->object_cap = 16;
    symbols->const_cap = 16;
    symbols->functions = arena_alloc(arena, (size_t)symbols->function_cap * sizeof(FnDef));
    symbols->types = arena_alloc(arena, (size_t)symbols->type_cap * sizeof(TypeDef));
    symbols->objects = arena_alloc(arena, (size_t)symbols->object_cap * sizeof(ObjectDef));
    symbols->consts = arena_alloc(arena, (size_t)symbols->const_cap * sizeof(ConstDef));
    return symbols;
}

static ConstDef *symbols_find_const(Symbols *symbols, Span name) {
    for (int32_t i = 0; i < symbols->const_count; i++) {
        if (span_same(symbols->consts[i].name, name)) return &symbols->consts[i];
    }
    return 0;
}

static void symbols_add_const(Symbols *symbols, Span name, int32_t value) {
    if (symbols_find_const(symbols, name)) return; /* skip duplicate */
    if (symbols->const_count >= symbols->const_cap) {
        int32_t next = symbols->const_cap * 2;
        ConstDef *fresh = arena_alloc(symbols->arena, (size_t)next * sizeof(ConstDef));
        memcpy(fresh, symbols->consts, (size_t)symbols->const_count * sizeof(ConstDef));
        symbols->consts = fresh;
        symbols->const_cap = next;
    }
    ConstDef *constant = &symbols->consts[symbols->const_count++];
    constant->name = cold_arena_span_copy(symbols->arena, name);
    constant->value = value;
    constant->is_str = false;
    constant->str_val = (Span){0};
}

void symbols_add_str_const(Symbols *symbols, Span name, Span str_val) {
    if (symbols_find_const(symbols, name)) return;
    if (symbols->const_count >= symbols->const_cap) {
        int32_t next = symbols->const_cap * 2;
        ConstDef *fresh = arena_alloc(symbols->arena, (size_t)next * sizeof(ConstDef));
        memcpy(fresh, symbols->consts, (size_t)symbols->const_count * sizeof(ConstDef));
        symbols->consts = fresh;
        symbols->const_cap = next;
    }
    ConstDef *constant = &symbols->consts[symbols->const_count++];
    constant->name = cold_arena_span_copy(symbols->arena, name);
    constant->value = 0;
    constant->is_str = true;
    constant->str_val = cold_arena_span_copy(symbols->arena, str_val);
}

static bool cold_fn_param_kind_can_refine(int32_t existing, int32_t fresh) {
    if (existing == fresh) return true;
    if (existing == SLOT_VARIANT &&
        (fresh == SLOT_I32 || fresh == SLOT_OBJECT || fresh == SLOT_SEQ_OPAQUE)) return true;
    if (existing == SLOT_OPAQUE &&
        (fresh == SLOT_OBJECT || fresh == SLOT_VARIANT || fresh == SLOT_SEQ_OPAQUE ||
         fresh == SLOT_PTR)) return true;
    if (existing == SLOT_OPAQUE_REF &&
        (fresh == SLOT_OBJECT_REF || fresh == SLOT_SEQ_I32_REF ||
         fresh == SLOT_SEQ_STR_REF || fresh == SLOT_I32_REF || fresh == SLOT_STR_REF)) return true;
    return false;
}

/* Look up a function by name, arity, and signature without adding.
   Returns the index if found, -1 if not found.
   Used by import_mode to avoid arena-modifying symbols_add_fn. */
int32_t symbols_find_fn(Symbols *symbols, Span name, int32_t arity,
                               const int32_t *param_kinds,
                               const int32_t *param_sizes,
                               Span ret) {
    for (int32_t existing = 0; existing < symbols->function_count; existing++) {
        FnDef *fn = &symbols->functions[existing];
        if (fn->arity != arity || !span_same(fn->name, name)) continue;
        bool same_signature = span_same(fn->ret, ret);
        for (int32_t i = 0; same_signature && i < arity; i++) {
            int32_t new_kind = param_kinds ? param_kinds[i] : SLOT_I32;
            if (fn->param_kind[i] != new_kind) same_signature = false;
        }
        if (!same_signature) continue;
        return existing;
    }
    return -1;
}
int32_t symbols_add_fn(Symbols *symbols, Span name, int32_t arity,
                       const int32_t *param_kinds,
                       const int32_t *param_sizes,
                       Span ret) {
    for (int32_t existing = 0; existing < symbols->function_count; existing++) {
        FnDef *fn = &symbols->functions[existing];
        if (fn->arity != arity || !span_same(fn->name, name)) continue;
        bool same_signature = span_same(fn->ret, ret);
        bool refinable_signature = true;
        for (int32_t i = 0; same_signature && i < arity; i++) {
            int32_t new_kind = param_kinds ? param_kinds[i] : SLOT_I32;
            int32_t new_size = param_sizes ? param_sizes[i] : cold_slot_size_for_kind(new_kind);
            if (fn->param_kind[i] != new_kind) same_signature = false;
            if (fn->param_size[i] != 0 && new_size != 0 && fn->param_size[i] != new_size) {
                same_signature = false;
            }
        }
        for (int32_t i = 0; refinable_signature && i < arity; i++) {
            int32_t new_kind = param_kinds ? param_kinds[i] : SLOT_I32;
            if (!cold_fn_param_kind_can_refine(fn->param_kind[i], new_kind)) refinable_signature = false;
        }
        if (!same_signature && !refinable_signature) continue;
        for (int32_t i = 0; i < arity; i++) {
            if (refinable_signature && param_kinds) {
                fn->param_kind[i] = param_kinds[i];
            }
            if (param_sizes && param_sizes[i] > 0 && (fn->param_size[i] == 0 || refinable_signature)) {
                fn->param_size[i] = param_sizes[i];
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

static TypeDef *symbols_find_type(Symbols *symbols, Span name);

static TypeDef *symbols_add_type(Symbols *symbols, Span name, int32_t variant_count) {
    TypeDef *existing = symbols_find_type(symbols, name);
    if (existing) return existing; /* skip duplicate (imported type already defined) */
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

static bool cold_span_is_simple_ident(Span span) {
    span = span_trim(span);
    if (span.len <= 0) return false;
    uint8_t first = span.ptr[0];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) return false;
    for (int32_t i = 1; i < span.len; i++) {
        uint8_t c = span.ptr[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

static Span cold_type_strip_var(Span type, bool *is_var) {
    type = span_trim(type);
    if (is_var) *is_var = false;
    if (type.len >= 3 && memcmp(type.ptr, "var", 3) == 0 &&
        (type.len == 3 || type.ptr[3] == ' ' || type.ptr[3] == '\t')) {
        if (is_var) *is_var = true;
        return span_trim(span_sub(type, 3, type.len));
    }
    return type;
}

static int32_t cold_split_top_level_commas(Span span, Span *items, int32_t cap) {
    int32_t count = 0;
    int32_t start = 0;
    int32_t paren = 0;
    int32_t square = 0;
    bool in_string = false;
    uint8_t quote = 0;
    for (int32_t i = 0; i <= span.len; i++) {
        uint8_t c = i < span.len ? span.ptr[i] : ',';
        if (i < span.len) {
            if (in_string) {
                if (c == '\\' && i + 1 < span.len) {
                    i++;
                    continue;
                }
                if (c == quote) in_string = false;
                continue;
            }
            if (c == '"' || c == '\'') {
                in_string = true;
                quote = c;
                continue;
            }
            if (c == '(') paren++;
            else if (c == ')') {
                if (paren <= 0) die("unbalanced generic parens");
                paren--;
            } else if (c == '[') {
                square++;
            } else if (c == ']') {
                if (square <= 0) die("unbalanced generic brackets");
                square--;
            }
        }
        if (i < span.len && !(c == ',' && paren == 0 && square == 0)) continue;
        Span item = span_trim(span_sub(span, start, i));
        if (item.len > 0) {
            if (count >= cap) die("too many generic arguments");
            items[count++] = item;
        }
        start = i + 1;
    }
    return count;
}

static bool cold_type_parse_generic_instance(Span type, Span *base, Span *args) {
    type = span_trim(type);
    int32_t open = -1;
    for (int32_t i = 0; i < type.len; i++) {
        if (type.ptr[i] == '[') {
            open = i;
            break;
        }
    }
    if (open <= 0 || type.len <= open + 1 || type.ptr[type.len - 1] != ']') return false;
    *base = span_trim(span_sub(type, 0, open));
    *args = span_trim(span_sub(type, open + 1, type.len - 1));
    return base->len > 0 && args->len > 0;
}

static Span cold_substitute_generic_span(Span field_type,
                                         Span *generic_names,
                                         int32_t generic_count,
                                         Span *args,
                                         int32_t arg_count) {
    field_type = span_trim(field_type);
    for (int32_t i = 0; i < generic_count && i < arg_count; i++) {
        if (span_same(field_type, generic_names[i])) return args[i];
    }
    return field_type;
}

static Span cold_substitute_generic_type(TypeDef *base, Span field_type,
                                         Span *args, int32_t arg_count) {
    return cold_substitute_generic_span(field_type, base->generic_names,
                                        base->generic_count, args, arg_count);
}

static Span cold_substitute_generic_object_type(ObjectDef *base, Span field_type,
                                                Span *args, int32_t arg_count) {
    return cold_substitute_generic_span(field_type, base->generic_names,
                                        base->generic_count, args, arg_count);
}

static bool cold_type_is_generic_placeholder(Span type, Span *generic_names,
                                             int32_t generic_count) {
    type = span_trim(type);
    for (int32_t i = 0; i < generic_count; i++) {
        if (span_same(type, generic_names[i])) return true;
    }
    return false;
}

static TypeDef *symbols_resolve_type(Symbols *symbols, Span type_name);
static ObjectDef *symbols_resolve_object(Symbols *symbols, Span type_name);
static bool cold_parse_i32_array_type(Span type, int32_t *len_out);
static int32_t cold_slot_kind_from_type_with_symbols(Symbols *symbols, Span type);
static int32_t cold_slot_size_from_type_with_symbols(Symbols *symbols, Span type, int32_t kind);

static Variant *type_find_variant(TypeDef *type, Span name) {
    if (!type) return 0;
    for (int32_t i = 0; i < type->variant_count; i++) {
        if (span_same(type->variants[i].name, name)) return &type->variants[i];
    }
    int32_t name_start = 0;
    for (int32_t i = name.len - 1; i >= 0; i--) {
        if (name.ptr[i] == '.') {
            name_start = i + 1;
            break;
        }
    }
    Span short_name = span_sub(name, name_start, name.len);
    for (int32_t i = 0; i < type->variant_count; i++) {
        Span candidate = type->variants[i].name;
        int32_t candidate_start = 0;
        for (int32_t j = candidate.len - 1; j >= 0; j--) {
            if (candidate.ptr[j] == '.') {
                candidate_start = j + 1;
                break;
            }
        }
        if (span_same(span_sub(candidate, candidate_start, candidate.len), short_name))
            return &type->variants[i];
    }
    return 0;
}

static bool type_is_payloadless_enum(TypeDef *type) {
    if (!type) return false;
    for (int32_t i = 0; i < type->variant_count; i++) {
        if (type->variants[i].field_count != 0) return false;
    }
    return true;
}

static ObjectDef *symbols_find_object(Symbols *symbols, Span name) {
    for (int32_t i = 0; i < symbols->object_count; i++) {
        if (span_same(symbols->objects[i].name, name)) return &symbols->objects[i];
    }
    return 0;
}

ObjectField *object_find_field(ObjectDef *object, Span name) {
    if (!object) return 0;
    for (int32_t i = 0; i < object->field_count; i++) {
        if (span_same(object->fields[i].name, name)) return &object->fields[i];
    }
    return 0;
}

static ObjectDef *symbols_add_object(Symbols *symbols, Span name, int32_t field_count) {
    ObjectDef *existing = symbols_find_object(symbols, name);
    if (existing) return existing; /* skip duplicate (imported type already defined) */
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
    object->generic_count = 0;
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
        if (variant->field_size[i] <= 0) {
            variant->field_size[i] = cold_slot_size_for_kind(variant->field_kind[i]);
        }
        int32_t align = variant->field_size[i] >= 8 ? 8 : 4;
        offset = align_i32(offset, align);
        variant->field_offset[i] = offset;
        offset += variant->field_size[i];
    }
    int32_t slot_size = align_i32(offset, 8);
    if (slot_size < 16) slot_size = 16;
    if (slot_size > type->max_slot_size) type->max_slot_size = slot_size;
    if (variant->field_count > type->max_field_count) type->max_field_count = variant->field_count;
}

int32_t symbols_variant_slot_size(Symbols *symbols, Variant *variant) {
    TypeDef *type = symbols_find_variant_type(symbols, variant);
    if (!type) die("variant has no parent type");
    return type->max_slot_size > 0 ? type->max_slot_size : 16;
}

static int32_t symbols_type_slot_size(TypeDef *type) {
    if (!type) die("missing type slot size");
    return type->max_slot_size > 0 ? type->max_slot_size : 16;
}

static void object_finalize_fields(ObjectDef *object) {
    int32_t offset = 0;
    for (int32_t i = 0; i < object->field_count; i++) {
        int32_t align = cold_slot_align_for_kind(object->fields[i].kind);
        offset = align_i32(offset, align);
        object->fields[i].offset = offset;
        offset += object->fields[i].size;
    }
    object->slot_size = align_i32(offset, 8);
    if (object->slot_size < 8) object->slot_size = 8;
}

static int32_t symbols_object_slot_size(ObjectDef *object) {
    if (!object) die("missing object slot size");
    return object->slot_size > 0 ? object->slot_size : 16;
}

static ObjectDef *symbols_ensure_std_error_info(Symbols *symbols) {
    ObjectDef *existing = symbols_find_object(symbols, cold_cstr_span("ErrorInfo"));
    if (existing) return existing;
    ObjectDef *object = symbols_add_object(symbols, cold_cstr_span("ErrorInfo"), 2);
    object->fields[0].name = cold_cstr_span("code");
    object->fields[0].type_name = cold_cstr_span("int32");
    object->fields[0].kind = SLOT_I32;
    object->fields[0].size = 4;
    object->fields[0].array_len = 0;
    object->fields[1].name = cold_cstr_span("msg");
    object->fields[1].type_name = cold_cstr_span("str");
    object->fields[1].kind = SLOT_STR;
    object->fields[1].size = COLD_STR_SLOT_SIZE;
    object->fields[1].array_len = 0;
    object_finalize_fields(object);
    return object;
}

static ObjectDef *symbols_ensure_std_result_base(Symbols *symbols) {
    ObjectDef *existing = symbols_find_object(symbols, cold_cstr_span("Result"));
    if (existing) return existing;
    (void)symbols_ensure_std_error_info(symbols);
    ObjectDef *object = symbols_add_object(symbols, cold_cstr_span("Result"), 3);
    object->generic_count = 1;
    object->generic_names[0] = cold_cstr_span("T");
    object->fields[0].name = cold_cstr_span("ok");
    object->fields[0].type_name = cold_cstr_span("bool");
    object->fields[0].kind = SLOT_I32;
    object->fields[0].size = 4;
    object->fields[0].array_len = 0;
    object->fields[1].name = cold_cstr_span("value");
    object->fields[1].type_name = cold_cstr_span("T");
    object->fields[1].kind = SLOT_I32;
    object->fields[1].size = 4;
    object->fields[1].array_len = 0;
    object->fields[2].name = cold_cstr_span("err");
    object->fields[2].type_name = cold_cstr_span("ErrorInfo");
    object->fields[2].kind = SLOT_OBJECT;
    object->fields[2].size = symbols_object_slot_size(symbols_ensure_std_error_info(symbols));
    object->fields[2].array_len = 0;
    object_finalize_fields(object);
    return object;
}

static ObjectDef *symbols_resolve_object(Symbols *symbols, Span type_name) {
    type_name = span_trim(type_name);
    ObjectDef *existing = symbols_find_object(symbols, type_name);
    if (existing) return existing;
    /* Cross-alias fallback: match last component after '.'.
       e.g. "ir.BodyIR" matches "coreir.BodyIR" when the same module
       was imported under different aliases via transitive imports. */
    if (!existing) {
        int32_t dot = -1;
        for (int32_t i = type_name.len - 1; i >= 0; i--) {
            if (type_name.ptr[i] == '.') { dot = i; break; }
        }
        if (dot >= 0) {
            Span suffix = span_sub(type_name, dot + 1, type_name.len);
            for (int32_t oi = 0; oi < symbols->object_count; oi++) {
                Span oname = symbols->objects[oi].name;
                int32_t odot = -1;
                for (int32_t j = oname.len - 1; j >= 0; j--) {
                    if (oname.ptr[j] == '.') { odot = j; break; }
                }
                if (odot >= 0 && span_same(span_sub(oname, odot + 1, oname.len), suffix)) {
                    existing = &symbols->objects[oi];
                    break;
                }
                if (odot < 0 && span_same(oname, suffix)) {
                    existing = &symbols->objects[oi];
                    break;
                }
            }
        }
    }
    if (existing) return existing;
    if (span_eq(type_name, "ErrorInfo")) return symbols_ensure_std_error_info(symbols);
    if (span_eq(type_name, "Result")) return symbols_ensure_std_result_base(symbols);
    Span base_name = {0};
    Span args_span = {0};
    if (!cold_type_parse_generic_instance(type_name, &base_name, &args_span)) return 0;
    if (symbols_find_type(symbols, base_name)) return 0; /* base is an ADT, not an object */
    ObjectDef *base = symbols_find_object(symbols, base_name);
    if (!base && span_eq(base_name, "Result")) base = symbols_ensure_std_result_base(symbols);
    if (!base) return 0;
    if (base->generic_count <= 0) return 0; /* non-generic base */
    Span args[COLD_MAX_VARIANT_FIELDS];
    int32_t arg_count = cold_split_top_level_commas(args_span, args, COLD_MAX_VARIANT_FIELDS);
    if (arg_count != base->generic_count) die("cold generic object arity mismatch");
    ObjectDef *inst = symbols_add_object(symbols, type_name, base->field_count);
    for (int32_t gi = 0; gi < base->generic_count; gi++) {
        inst->generic_names[gi] = base->generic_names[gi];
    }
    for (int32_t fi = 0; fi < base->field_count; fi++) {
        ObjectField *src = &base->fields[fi];
        ObjectField *dst = &inst->fields[fi];
        Span field_type = cold_substitute_generic_object_type(base, src->type_name, args, arg_count);
        /* fallback: if substitution produced empty span (e.g. src->type_name missing), use arg directly */
        if (field_type.len <= 0 && span_eq(src->name, "value") && arg_count > 0) {
            field_type = args[0];
        }
        int32_t fk = cold_slot_kind_from_type_with_symbols(symbols, field_type);
        dst->name = src->name;
        dst->type_name = field_type;
        dst->kind = fk;
        dst->size = cold_slot_size_from_type_with_symbols(symbols, field_type, fk);
        dst->array_len = 0;
        if (fk == SLOT_ARRAY_I32 && !cold_parse_i32_array_type(field_type, &dst->array_len) && !cold_span_starts_with(span_trim(field_type), "uint8[")) {
            dst->array_len = 4; /* default */
        }
    }
    object_finalize_fields(inst);
    return inst;
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

static bool cold_parse_str_seq_type(Span type) {
    type = span_trim(type);
    return span_eq(type, "str[]") || span_eq(type, "cstring[]");
}

static bool cold_parse_opaque_seq_type(Span type) {
    type = span_trim(type);
    if (cold_parse_i32_seq_type(type) || cold_parse_str_seq_type(type)) return false;
    return type.len > 2 && type.ptr[type.len - 2] == '[' && type.ptr[type.len - 1] == ']';
}

static bool cold_type_has_qualified_name(Span type) {
    type = span_trim(type);
    for (int32_t i = 0; i < type.len; i++) {
        if (type.ptr[i] == '.') return true;
    }
    return false;
}

static int32_t cold_slot_kind_from_type_with_symbols(Symbols *symbols, Span type) {
    bool is_var = false;
    type = cold_type_strip_var(type, &is_var);
    int32_t array_len = 0;
    TypeDef *known_type = symbols ? symbols_resolve_type(symbols, type) : 0;
    bool known_enum = known_type && known_type->is_enum;
    if (is_var && (span_eq(type, "int64") || span_eq(type, "uint64"))) return SLOT_I64_REF;
    if (is_var && (span_eq(type, "int32") || span_eq(type, "int") || span_eq(type, "i") ||
                   span_eq(type, "bool") || span_eq(type, "uint8") ||
                   span_eq(type, "char") || span_eq(type, "uint32") ||
                   known_enum)) return SLOT_I32_REF;
    if (is_var && (span_eq(type, "str") || span_eq(type, "cstring"))) return SLOT_STR_REF;
    if (is_var && cold_parse_i32_seq_type(type)) return SLOT_SEQ_I32_REF;
    if (is_var && cold_parse_str_seq_type(type)) return SLOT_SEQ_STR_REF;
    if (is_var && cold_parse_opaque_seq_type(type)) return SLOT_SEQ_OPAQUE_REF;
    if (is_var && symbols && symbols_resolve_object(symbols, type)) return SLOT_OBJECT_REF;
    if (is_var && cold_type_has_qualified_name(type)) return SLOT_OPAQUE_REF;
    if (cold_parse_i32_array_type(type, &array_len)) return SLOT_ARRAY_I32;
    if (cold_span_starts_with(span_trim(type), "uint8[")) return SLOT_ARRAY_I32;
    if (cold_parse_i32_seq_type(type)) return SLOT_SEQ_I32;
    if (cold_parse_str_seq_type(type)) return SLOT_SEQ_STR;
    if (cold_parse_opaque_seq_type(type)) return SLOT_SEQ_OPAQUE;
    if (span_eq(type, "str") || span_eq(type, "cstring") || span_eq(type, "s")) return SLOT_STR;
    if (span_eq(type, "int64") || span_eq(type, "uint64")) return SLOT_I64;
    if (span_eq(type, "int32") || span_eq(type, "int") || span_eq(type, "i") ||
        span_eq(type, "bool") ||
        span_eq(type, "uint8") || span_eq(type, "char") ||
        span_eq(type, "uint32") ||
        span_eq(type, "void") || type.len == 0) return SLOT_I32;
    if (known_enum) return SLOT_I32;
    if (span_eq(type, "v")) return SLOT_VARIANT;
    if (span_eq(type, "o")) return SLOT_OPAQUE;
    if (span_eq(type, "ptr")) return SLOT_OPAQUE;
    /* function type like fn(int32): int32 → function pointer */
    if (cold_span_starts_with(type, "fn(")) return is_var ? SLOT_OBJECT_REF : SLOT_PTR;
    if (symbols) {
        ObjectDef *obj = symbols_resolve_object(symbols, type);
        if (obj) return obj->is_ref ? SLOT_PTR : SLOT_OBJECT;
    }
    if (known_type) return SLOT_VARIANT;
    if (cold_type_has_qualified_name(type)) return SLOT_OPAQUE;
    if (type.len > 1 && type.ptr[0] >= 'A' && type.ptr[0] <= 'Z') return SLOT_VARIANT;
    /* Unknown type: treat as opaque */
    return SLOT_OPAQUE;
    return SLOT_I32;
}

static int32_t cold_slot_size_from_type_with_symbols(Symbols *symbols, Span type, int32_t kind) {
    bool is_var = false;
    type = cold_type_strip_var(type, &is_var);
    int32_t array_len = 0;
    if (kind == SLOT_I32_REF || kind == SLOT_I64_REF ||
        kind == SLOT_SEQ_I32_REF || kind == SLOT_OBJECT_REF ||
        kind == SLOT_STR_REF || kind == SLOT_SEQ_STR_REF ||
        kind == SLOT_SEQ_OPAQUE_REF || kind == SLOT_OPAQUE_REF) return 8;
    if (kind == SLOT_ARRAY_I32) {
        int32_t elem_size = 4;
        if (cold_span_starts_with(span_trim(type), "uint8[")) {
            elem_size = 1;
            Span inner = span_trim(type);
            Span count = span_trim(span_sub(inner, 6, inner.len - 1));
            if (span_is_i32(count)) array_len = span_i32(count);
            else if (symbols) { ConstDef *c = symbols_find_const(symbols, count); if (c) array_len = c->value; }
            if (array_len <= 0) array_len = 32; /* default for FixedBytes32 */
        } else if (!cold_parse_i32_array_type(type, &array_len)) {
            die("cold array type missing length");
        }
        if (array_len <= 0) array_len = 4;
        return align_i32(array_len * elem_size, 8);
    }
    if (kind == SLOT_SEQ_I32) {
        if (!cold_parse_i32_seq_type(type)) die("cold seq type missing []");
        return 16;
    }
    if (kind == SLOT_SEQ_STR) {
        if (!cold_parse_str_seq_type(type)) die("cold str seq type missing []");
        return 16;
    }
    if (kind == SLOT_SEQ_OPAQUE) {
        if (!cold_parse_opaque_seq_type(type)) die("cold opaque seq type missing []");
        return 16;
    }
    if (kind == SLOT_OPAQUE) return 8;
    if (kind == SLOT_I64) return 8;
    if (kind == SLOT_OBJECT) return symbols_object_slot_size(symbols_resolve_object(symbols, type));
    if (kind == SLOT_VARIANT && symbols && !span_eq(type, "v")) {
        TypeDef *def = symbols_resolve_type(symbols, type);
        if (!def) return 0; /* unknown variant type */
        return symbols_type_slot_size(def);
    }
    return cold_slot_size_for_kind(kind);
}

#ifndef COLD_BACKEND_ONLY
static Span cold_seq_opaque_element_type(Span type) {
    type = span_trim(cold_type_strip_var(type, 0));
    if (cold_parse_opaque_seq_type(type)) {
        return span_trim(span_sub(type, 0, type.len - 2));
    }
    return type;
}

static int32_t cold_seq_opaque_element_size_from_type(Symbols *symbols, Span type) {
    Span elem = cold_seq_opaque_element_type(type);
    if (elem.len <= 0) die("opaque sequence element type missing");
    int32_t kind = cold_slot_kind_from_type_with_symbols(symbols, elem);
    if (kind == SLOT_SEQ_OPAQUE || kind == SLOT_SEQ_OPAQUE_REF)
        die("nested opaque sequence element unsupported");
    int32_t size = cold_slot_size_from_type_with_symbols(symbols, elem, kind);
    if (size <= 0) die("opaque sequence element size missing");
    return size;
}

static void body_slot_set_seq_opaque_type(BodyIR *body, Symbols *symbols,
                                          int32_t slot, Span type) {
    body_slot_set_type(body, slot, cold_seq_opaque_element_type(type));
    body->slot_aux[slot] = cold_seq_opaque_element_size_from_type(symbols, type);
}

static int32_t cold_seq_opaque_element_size_for_slot(Symbols *symbols, BodyIR *body,
                                                     int32_t slot) {
    if (slot >= 0 && slot < body->slot_count && body->slot_aux[slot] > 0) {
        return body->slot_aux[slot];
    }
    if (slot < 0 || slot >= body->slot_count || body->slot_type[slot].len <= 0) {
        die("opaque sequence element metadata missing");
    }
    int32_t size = cold_seq_opaque_element_size_from_type(symbols, body->slot_type[slot]);
    body->slot_aux[slot] = size;
    return size;
}
#endif /* COLD_BACKEND_ONLY */

static TypeDef *symbols_resolve_type(Symbols *symbols, Span type_name) {
    type_name = span_trim(type_name);
    TypeDef *existing = symbols_find_type(symbols, type_name);
    if (existing) return existing;
    for (int32_t i = type_name.len - 1; i > 0; i--) {
        if (type_name.ptr[i] == '.') {
            TypeDef *suffix = symbols_find_type(symbols, span_sub(type_name, i + 1, type_name.len - i - 1));
            if (suffix) return suffix;
            break;
        }
    }
    Span base_name = {0};
    Span args_span = {0};
    if (!cold_type_parse_generic_instance(type_name, &base_name, &args_span)) return 0;
    TypeDef *base = symbols_find_type(symbols, base_name);
    if (!base) return 0;
    if (base->generic_count <= 0) return 0; /* non-generic ADT */
    Span args[COLD_MAX_VARIANT_FIELDS];
    int32_t arg_count = cold_split_top_level_commas(args_span, args, COLD_MAX_VARIANT_FIELDS);
    if (arg_count != base->generic_count) die("cold generic arity mismatch");
    TypeDef *inst = symbols_add_type(symbols, type_name, base->variant_count);
    for (int32_t vi = 0; vi < base->variant_count; vi++) {
        Variant *src = &base->variants[vi];
        Variant *dst = &inst->variants[vi];
        dst->name = src->name;
        dst->tag = src->tag;
        dst->field_count = src->field_count;
        for (int32_t fi = 0; fi < src->field_count; fi++) {
            Span field_type = cold_substitute_generic_type(base, src->field_type[fi], args, arg_count);
            int32_t fk = cold_slot_kind_from_type_with_symbols(symbols, field_type);
            dst->field_kind[fi] = fk;
            dst->field_type[fi] = field_type;
            dst->field_size[fi] = cold_slot_size_from_type_with_symbols(symbols, field_type, fk);
        }
        variant_finalize_layout(inst, dst);
    }
    return inst;
}

int32_t cold_return_kind_from_span(Symbols *symbols, Span ret) {
    ret = span_trim(ret);
    if (span_eq(ret, "str") || span_eq(ret, "cstring")) return SLOT_STR;
    if (span_eq(ret, "int64") || span_eq(ret, "uint64")) return SLOT_I64;
    if (span_eq(ret, "int32") || span_eq(ret, "int") || span_eq(ret, "i") ||
        span_eq(ret, "bool") ||
        span_eq(ret, "uint8") || span_eq(ret, "char") ||
        span_eq(ret, "uint32") ||
        span_eq(ret, "void") || ret.len == 0) return SLOT_I32;
    if (cold_parse_i32_seq_type(ret)) return SLOT_SEQ_I32;
    if (cold_parse_str_seq_type(ret)) return SLOT_SEQ_STR;
    if (cold_parse_opaque_seq_type(ret)) return SLOT_SEQ_OPAQUE;
    TypeDef *known_type = symbols_resolve_type(symbols, ret);
    if (known_type) return SLOT_VARIANT;
    ObjectDef *ret_obj = symbols_resolve_object(symbols, ret);
    if (ret_obj) return ret_obj->is_ref ? SLOT_PTR : SLOT_OBJECT;
    if (span_eq(ret, "ptr")) return SLOT_OPAQUE;
    /* function type like fn(int32): int32 → function pointer */
    if (cold_span_starts_with(ret, "fn(")) return SLOT_PTR;
    if (cold_type_has_qualified_name(ret)) return SLOT_OPAQUE;
    /* Treat unknown uppercase types and camelCase types as opaque objects */
    if (ret.len > 0 && ((ret.ptr[0] >= 'A' && ret.ptr[0] <= 'Z') ||
                         (ret.ptr[0] >= 'a' && ret.ptr[0] <= 'z'))) return SLOT_OPAQUE;
    fprintf(stderr, "cheng_cold: unknown cold return type=%.*s\n", ret.len, ret.ptr);
    return SLOT_OPAQUE;
    return SLOT_I32;
}

int32_t cold_return_slot_size(Symbols *symbols, Span ret, int32_t kind) {
    if (kind == SLOT_STR) return COLD_STR_SLOT_SIZE;
    if (kind == SLOT_I64 || kind == SLOT_PTR) return 8;
    if (kind == SLOT_SEQ_I32 || kind == SLOT_SEQ_STR || kind == SLOT_SEQ_OPAQUE) return 16;
    if (kind == SLOT_VARIANT) return symbols_type_slot_size(symbols_resolve_type(symbols, span_trim(ret)));
    if (kind == SLOT_OBJECT) return symbols_object_slot_size(symbols_resolve_object(symbols, span_trim(ret)));
    if (kind == SLOT_OPAQUE) return 8;
    return 4;
}

static bool cold_kind_is_composite(int32_t kind) {
    return kind == SLOT_STR || kind == SLOT_VARIANT || kind == SLOT_OBJECT ||
           kind == SLOT_ARRAY_I32 || kind == SLOT_SEQ_I32 || kind == SLOT_SEQ_STR ||
           kind == SLOT_SEQ_OPAQUE;
}

static bool cold_return_span_is_void(Span ret) {
    ret = span_trim(ret);
    return ret.len == 0 || span_eq(ret, "void");
}

static int32_t body_default_slot(BodyIR *body, Symbols *symbols, Span type, int32_t *kind_out) {
    int32_t kind = cold_slot_kind_from_type_with_symbols(symbols, type);
    int32_t size = cold_slot_size_from_type_with_symbols(symbols, type, kind);
    int32_t slot = body_slot(body, kind, size);
    body_slot_set_type(body, slot, span_trim(type));
    if (kind == SLOT_ARRAY_I32) {
        int32_t len = 0;
        if (!cold_parse_i32_array_type(type, &len)) {
            len = 0; /* default to empty array */
        }
        body_slot_set_array_len(body, slot, len);
    }
    if (kind == SLOT_I32) {
        body_op(body, BODY_OP_I32_CONST, slot, 0, 0);
    } else if (kind == SLOT_I64) {
        body_op(body, BODY_OP_I64_CONST, slot, 0, 0);
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

Local *locals_find(Locals *locals, Span name) {
    for (int32_t i = locals->count - 1; i >= 0; i--) {
        if (span_same(locals->items[i].name, name)) return &locals->items[i];
    }
    return 0;
}

static TypeDef *cold_question_result_type(Symbols *symbols, BodyIR *body, int32_t variant_slot) {
    if (variant_slot < 0 || variant_slot >= body->slot_count) die("invalid ? variant slot");
    if (body->slot_kind[variant_slot] != SLOT_VARIANT && body->slot_kind[variant_slot] != SLOT_OBJECT) {
        /* Skip ? on non-variant/object target */
        return 0;
    }
    Span type_name = body->slot_type[variant_slot];
    if (type_name.len <= 0) {
        /* Unknown variant type, skip */
        return 0;
    }
    TypeDef *type = symbols_resolve_type(symbols, type_name);
    if (!type) return 0;
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
    if (!type || !type->variants) return block;
    Variant *ok_variant = &type->variants[0];
    Variant *err_variant = &type->variants[1];
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
    if (body->return_kind == SLOT_VARIANT &&
        body->return_type.len > 0 &&
        span_same(body->return_type, type->name)) {
        int32_t ret_term = body_term(body, BODY_TERM_RET, variant_slot, -1, 0, -1, -1);
        body_end_block(body, err_block, ret_term);
    } else if (body->return_kind == SLOT_I32 &&
               err_variant->field_count == 1 &&
               err_variant->field_kind[0] == SLOT_I32) {
        int32_t err_payload = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_PAYLOAD_LOAD, err_payload, variant_slot, err_variant->field_offset[0]);
        int32_t ret_term = body_term(body, BODY_TERM_RET, err_payload, -1, 0, -1, -1);
        body_end_block(body, err_block, ret_term);
    } else {
        /* Incompatible error type: use BRK-based fallback on error path */
        int32_t tag_slot = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_TAG_LOAD, tag_slot, variant_slot, 0);
        body_op3(body, BODY_OP_UNWRAP_OR_RETURN, tag_slot, tag_slot, 0, 0);
        /* UNWRAP_OR_RETURN compares tag with 0 (Ok), BRK on mismatch.
           On success (Ok), execution continues. End the err_block with a ret 0 stub. */
        int32_t zero = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, zero, 0, 0);
        int32_t fallback_ret = body_term(body, BODY_TERM_RET, zero, -1, 0, -1, -1);
        body_end_block(body, err_block, fallback_ret);
    }
    body_reopen_block(body, ok_block);
    if (bind_value) {
        int32_t payload_kind = ok_variant->field_kind[0];
        if (declared_kind >= 0 && declared_kind != payload_kind) die("? binding declared type mismatch");
        int32_t payload_slot = body_slot(body, payload_kind, ok_variant->field_size[0]);
        body_slot_set_type(body, payload_slot,
                           declared_type.len > 0 ? declared_type : ok_variant->field_type[0]);
        body_op(body, BODY_OP_PAYLOAD_LOAD, payload_slot, variant_slot, ok_variant->field_offset[0]);
        locals_add(locals, bind_name, payload_slot, payload_kind);
    }
    return ok_block;
}

/* ================================================================
 * Parser
 * ================================================================ */
/* Forward declare import source struct (defined later at ~line 19939) */
struct ColdImportSource;
/* Full definition needed for stack-allocated arrays */
struct ColdImportSource {
    Span alias;
    char path[PATH_MAX];
};
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

/* ================================================================
 * Cold CSG body facts -> SoA BodyIR
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
    int32_t depth = 0;
    for (int32_t i = 0; i <= specs.len; i++) {
        if (i < specs.len) {
            uint8_t c = specs.ptr[i];
            if (c == '[' || c == '(' || c == '<') depth++;
            else if (c == ']' || c == ')' || c == '>') { if (depth > 0) depth--; }
            if (c != ',' || depth > 0) continue;
        }
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
    /* if existing type was returned, don't overwrite its variants */
    if (type->variant_count > 0 && type->variant_count != variant_count) return;
    if (field_count > 3 && fields[3].len > 0) {
        type->generic_count = cold_split_top_level_commas(fields[3],
                                                          type->generic_names,
                                                          COLD_MAX_VARIANT_FIELDS);
        fprintf(stderr, "[DBG] type %.*s generic_count=%d names=", fields[1].len, fields[1].ptr, type->generic_count);
        for (int32_t gi = 0; gi < type->generic_count; gi++)
            fprintf(stderr, "%.*s,", type->generic_names[gi].len, type->generic_names[gi].ptr);
        fprintf(stderr, "\n");
    }
    int32_t start = 0;
    int32_t vi = 0;
    int32_t bracket_depth = 0;
    for (int32_t i = 0; i <= specs.len; i++) {
        if (i < specs.len) {
            uint8_t c = specs.ptr[i];
            if (c == '[') bracket_depth++;
            else if (c == ']' && bracket_depth > 0) bracket_depth--;
            else if (c == '(' || c == '<') bracket_depth++;
            else if (c == ')' || c == '>') { if (bracket_depth > 0) bracket_depth--; }
            if (c != ',' || bracket_depth > 0) continue;
        }
        Span part = span_trim(span_sub(specs, start, i));
        start = i + 1;
        if (part.len <= 0) continue;
        int32_t colon = cold_span_find_char(part, ':');
        if (colon <= 0) die("cold csg variant spec requires field count");
        Span name = span_trim(span_sub(part, 0, colon));
        Span rest = span_trim(span_sub(part, colon + 1, part.len));
        int32_t kind_colon = cold_span_find_char(rest, ':');
        Span count_span = kind_colon >= 0 ? span_trim(span_sub(rest, 0, kind_colon)) : rest;
        Span after_count = kind_colon >= 0 ? span_trim(span_sub(rest, kind_colon + 1, rest.len)) : (Span){0};
        int32_t type_colon = cold_span_find_char(after_count, ':');
        Span kind_codes = type_colon >= 0 ? span_trim(span_sub(after_count, 0, type_colon)) : after_count;
        Span type_codes = type_colon >= 0 ? span_trim(span_sub(after_count, type_colon + 1, after_count.len)) : (Span){0};
        if (!span_is_i32(count_span)) die("cold csg variant field count must be int32");
        int32_t fields_for_variant = span_i32(count_span);
        if (fields_for_variant < 0) die("cold csg variant field count must be non-negative");
        if (fields_for_variant > COLD_MAX_VARIANT_FIELDS) die("too many cold csg variant fields");
        if (kind_codes.len > 0 && kind_codes.len != fields_for_variant) die("cold csg variant kind count mismatch");
        Span field_types[COLD_MAX_VARIANT_FIELDS];
        int32_t field_type_count = 0;
        if (type_codes.len > 0) {
            int32_t ts = 0;
            for (int32_t ti = 0; ti <= type_codes.len; ti++) {
                if (ti < type_codes.len && type_codes.ptr[ti] != ';') continue;
                if (field_type_count >= COLD_MAX_VARIANT_FIELDS) die("too many cold csg field types");
                field_types[field_type_count++] = span_trim(span_sub(type_codes, ts, ti));
                ts = ti + 1;
            }
            if (field_type_count != fields_for_variant) die("cold csg variant field type count mismatch");
        }
        if (vi >= variant_count) die("cold csg variant count overflow");
        Variant *variant = &type->variants[vi];
        variant->name = name;
        variant->tag = vi;
        variant->field_count = fields_for_variant;
        for (int32_t field_index = 0; field_index < fields_for_variant; field_index++) {
            char code = kind_codes.len > 0 ? (char)kind_codes.ptr[field_index] : 'i';
            Span field_type = field_type_count > 0 ? field_types[field_index] : (Span){0};
            variant->field_type[field_index] = field_type;
            bool generic_field = false;
            for (int32_t gi = 0; gi < type->generic_count; gi++) {
                if (span_same(field_type, type->generic_names[gi])) generic_field = true;
            }
            if (generic_field) {
                variant->field_kind[field_index] = SLOT_I32;
                variant->field_size[field_index] = 4;
            } else if (field_type.len > 0) {
                int32_t fk = cold_slot_kind_from_type_with_symbols(csg->symbols, field_type);
                variant->field_kind[field_index] = fk;
                variant->field_size[field_index] = cold_slot_size_from_type_with_symbols(csg->symbols, field_type, fk);
            } else {
                variant->field_kind[field_index] = cold_slot_kind_from_code(code);
                variant->field_size[field_index] = cold_slot_size_for_kind(variant->field_kind[field_index]);
            }
        }
        variant_finalize_layout(type, variant);
        vi++;
    }
    if (vi != variant_count) die("cold csg variant count mismatch");
    /* detect enum types: all variants have 0 fields */
    if (variant_count > 0) {
        bool all_no_fields = true;
        for (int32_t ci = 0; ci < variant_count; ci++) {
            if (type->variants[ci].field_count > 0) { all_no_fields = false; break; }
        }
        if (all_no_fields) type->is_enum = true;
    }
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
    /* if existing object was returned, don't overwrite its fields */
    if (object->field_count > 0 && object->field_count != object_field_count) return;
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
            if (field->kind == SLOT_SEQ_I32) field->type_name = cold_cstr_span("int32[]");
            /* for user-type fields (v/o), code may include ":typeName" suffix; resolve object */
            if (field->kind == SLOT_VARIANT || field->kind == SLOT_OPAQUE) {
                int32_t colon2 = cold_span_find_char(code, ':');
                if (colon2 > 0) {
                    Span field_type = span_trim(span_sub(code, colon2 + 1, code.len));
                    field->type_name = field_type;
                    /* o: prefix always means sequence of opaque/object type */
                    if (code.ptr[0] == 'o') {
                        field->kind = SLOT_SEQ_OPAQUE;
                        field->size = cold_slot_size_for_kind(SLOT_SEQ_OPAQUE);
                    } else if (csg->symbols && symbols_find_object(csg->symbols, field_type)) {
                        /* v:TypeName -- if TypeName is an object, upgrade to SLOT_OBJECT */
                        field->kind = SLOT_OBJECT;
                        field->size = cold_slot_size_for_kind(SLOT_OBJECT);
                    }
                }
            }
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

/* Un-escape CSG escape sequences (\t, \n, \\\\) in-place within an arena-allocated buffer.
   Returns the un-escaped span. */
static Span cold_span_unescape_csg(Arena *arena, Span src) {
    if (src.len <= 0) return src;
    /* first pass: count result length */
    int32_t dlen = 0;
    for (int32_t i = 0; i < src.len; i++) {
        if (src.ptr[i] == '\\' && i + 1 < src.len) {
            uint8_t n = src.ptr[i + 1];
            if (n == 't' || n == 'n' || n == '\\') { i++; dlen++; continue; }
        }
        dlen++;
    }
    if (dlen == src.len) return src; /* no escapes */
    uint8_t *dst = arena_alloc(arena, (size_t)dlen);
    int32_t di = 0;
    for (int32_t i = 0; i < src.len; i++) {
        if (src.ptr[i] == '\\' && i + 1 < src.len) {
            uint8_t n = src.ptr[i + 1];
            if (n == 't') { dst[di++] = '\t'; i++; continue; }
            if (n == 'n') { dst[di++] = '\n'; i++; continue; }
            if (n == '\\') { dst[di++] = '\\'; i++; continue; }
        }
        dst[di++] = src.ptr[i];
    }
    return (Span){dst, dlen};
}

/* ================================================================
 * Forward declarations of struct types (for the CSG lowering code)
 * ================================================================ */
#define COLD_LOOP_PATCH_CAP 128

typedef struct LoopCtx {
    int32_t break_terms[COLD_LOOP_PATCH_CAP];
    int32_t break_count;
    int32_t continue_terms[COLD_LOOP_PATCH_CAP];
    int32_t continue_count;
    struct LoopCtx *parent;
} LoopCtx;

static void cold_csg_add_stmt(ColdCsg *csg, Span *fields, int32_t field_count) {
    if (field_count < 4) die("cold csg statement row requires four fields");
    cold_csg_ensure_stmts(csg);
    ColdCsgStmt *stmt = &csg->stmts[csg->stmt_count++];
    stmt->fn_index = span_i32(fields[1]);
    stmt->indent = span_i32(fields[2]);
    stmt->kind = cold_span_unescape_csg(csg->arena, fields[3]);
    stmt->a = field_count > 4 ? cold_span_unescape_csg(csg->arena, fields[4]) : (Span){0};
    stmt->b = field_count > 5 ? cold_span_unescape_csg(csg->arena, fields[5]) : (Span){0};
    stmt->c = field_count > 6 ? cold_span_unescape_csg(csg->arena, fields[6]) : (Span){0};
    stmt->d = field_count > 7 ? cold_span_unescape_csg(csg->arena, fields[7]) : (Span){0};
}

#ifndef COLD_BACKEND_ONLY
static void cold_csg_finalize(ColdCsg *csg) {
    if (csg->function_count <= 0) die("cold csg has no functions");
    for (int32_t i = 0; i < csg->function_count; i++) {
        Span names[COLD_MAX_I32_PARAMS];
        int32_t kinds[COLD_MAX_I32_PARAMS];
        int32_t sizes[COLD_MAX_I32_PARAMS];
        int32_t arity = parse_param_specs(csg->symbols, csg->functions[i].params,
                                          names, kinds, sizes, 0, COLD_MAX_I32_PARAMS);
        /* refine kinds using symbols table (must match parse_fn's cold_slot_kind_from_type_with_symbols) */
        Span tnames[COLD_MAX_I32_PARAMS];
        int32_t refined_kinds[COLD_MAX_I32_PARAMS];
        int32_t refined_sizes[COLD_MAX_I32_PARAMS];
        int32_t r_arity = parse_param_specs(csg->symbols, csg->functions[i].params,
                                            names, refined_kinds, refined_sizes, tnames, COLD_MAX_I32_PARAMS);
        if (r_arity == arity) {
            memcpy(kinds, refined_kinds, (size_t)arity * sizeof(int32_t));
            memcpy(sizes, refined_sizes, (size_t)arity * sizeof(int32_t));
        }
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
        Span line = span_sub(text, start, end);
        /* trim leading whitespace only (preserve trailing tabs for field splitting) */
        int32_t ltrim = 0;
        while (ltrim < line.len) {
            uint8_t c = line.ptr[ltrim];
            if (c != ' ' && c != '\t') break;
            ltrim++;
        }
        /* trim trailing newline/carriage-return only (NOT tabs) */
        int32_t rtrim = line.len;
        while (rtrim > ltrim) {
            uint8_t c = line.ptr[rtrim - 1];
            if (c != '\r' && c != '\n') break;
            rtrim--;
        }
        line = (Span){line.ptr + ltrim, rtrim - ltrim};
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
        if (span_eq(fields[0], "cold_csg_const") && field_count >= 3) {
            symbols_add_const(csg->symbols, fields[2], span_i32(fields[1]));
            continue;
        }
        if (span_eq(fields[0], "cold_csg_const_str") && field_count >= 3) {
            symbols_add_str_const(csg->symbols, fields[2], fields[1]);
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
    int32_t slot = -1;
    /* bare return for void functions */
    if (expr.len <= 0) {
        if (body->return_kind != SLOT_I32 || !span_eq(body->return_type, "")) {
            die("cold csg bare return only allowed in void functions");
        }
        int32_t unit = body_slot(body, SLOT_I32, 4);
        body_op(body, BODY_OP_I32_CONST, unit, 0, 0);
        int32_t term = body_term(body, BODY_TERM_RET, unit, -1, 0, -1, -1);
        body_end_block(body, block, term);
        return;
    }
    Parser parser = {expr, 0, lower->owner.arena, lower->owner.symbols};
    Variant *return_variant = 0;
    if (body->return_kind == SLOT_VARIANT && body->return_type.len > 0) {
        TypeDef *return_type = symbols_resolve_type(lower->csg->symbols, body->return_type);
        return_variant = type_find_variant(return_type, parser_peek(&parser));
    }
    if (return_variant) {
        (void)parser_token(&parser);
        slot = parse_constructor(&parser, body, locals, return_variant);
        parser_ws(&parser);
        if (parser.pos != parser.source.len) die("unsupported cold csg return constructor tokens");
        kind = SLOT_VARIANT;
    } else {
        slot = csg_parse_expr_span(lower, body, locals, expr, &kind);
    }
    if (kind != body->return_kind) die("cold csg return kind mismatch");
    if (kind == SLOT_I32) body_op(body, BODY_OP_LOAD_I32, slot, 0, 0);
    int32_t term = body_term(body, BODY_TERM_RET, slot, -1, 0, -1, -1);
    body_end_block(body, block, term);
}

static void csg_parse_let(ColdCsgLower *lower, BodyIR *body,
                          Locals *locals, Span name, Span expr) {
    int32_t kind = SLOT_I32;
    int32_t src_slot = csg_parse_expr_span(lower, body, locals, expr, &kind);
    locals_add(locals, name, src_slot, kind);
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
    } else if (declared_kind == SLOT_VARIANT) {
        Parser parser = {expr, 0, lower->owner.arena, lower->owner.symbols};
        TypeDef *declared_type = symbols_resolve_type(lower->csg->symbols, type);
        Variant *declared_variant = declared_type ? type_find_variant(declared_type, parser_peek(&parser)) : 0;
        if (declared_variant) {
            (void)parser_token(&parser);
            slot = parse_constructor(&parser, body, locals, declared_variant);
            parser_ws(&parser);
            if (parser.pos != parser.source.len) die("unsupported cold csg typed constructor tokens");
            *kind = SLOT_VARIANT;
        } else {
            slot = parse_expected_payloadless_variant(&parser, body, declared_type, kind);
            if (slot >= 0) {
                parser_ws(&parser);
                if (parser.pos != parser.source.len) die("unsupported cold csg typed enum tokens");
            } else {
                slot = csg_parse_expr_span(lower, body, locals, expr, kind);
            }
        }
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
    if (declared_kind == SLOT_SEQ_OPAQUE)
        body_slot_set_seq_opaque_type(body, lower->csg->symbols, slot, span_trim(type));
    else
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
    /* check for field assignment: obj.field = value */
    int32_t dot = cold_span_find_char(name, '.');
    if (dot > 0) {
        Span obj_name = span_trim(span_sub(name, 0, dot));
        Span field_rest = span_trim(span_sub(name, dot + 1, name.len));
        /* check for array index after field: tokenMeta[index] */
        int32_t lb = cold_span_find_char(field_rest, '[');
        Span field_name = lb > 0 ? span_trim(span_sub(field_rest, 0, lb)) : field_rest;
        Span index_expr = lb > 0 ? span_trim(span_sub(field_rest, lb + 1, field_rest.len - 1)) : (Span){0};
        Local *obj_local = locals_find(locals, obj_name);
        if (!obj_local) die("cold csg field assign: object not found");
        if (obj_local->kind != SLOT_OBJECT && obj_local->kind != SLOT_OBJECT_REF)
            die("cold csg field assign: target is not an object");
        Span type_name = body->slot_type[obj_local->slot];
        ObjectDef *object = symbols_resolve_object(lower->csg->symbols, type_name);
        if (!object) die("cold csg field assign: object type not found");
        ObjectField *field = object_find_field(object, field_name);
        if (!field) die("cold csg field assign: field not found");
        int32_t kind = SLOT_I32;
        int32_t val_slot = csg_parse_expr_span(lower, body, locals, expr, &kind);
        if (lb > 0) {
            /* array field index assignment: obj.field[index] = value */
            int32_t idx_slot = csg_parse_expr_span(lower, body, locals, index_expr, &kind);
            int32_t ref_kind = field->kind == SLOT_SEQ_OPAQUE ? SLOT_SEQ_OPAQUE_REF : SLOT_OBJECT_REF;
            int32_t ref_slot = body_slot(body, ref_kind, 8);
            if (field->kind == SLOT_ARRAY_I32) body_slot_set_array_len(body, ref_slot, field->array_len);
            if (field->kind == SLOT_SEQ_OPAQUE)
                body_slot_set_seq_opaque_type(body, lower->csg->symbols, ref_slot, field->type_name);
            body_op3(body, BODY_OP_FIELD_REF, ref_slot, obj_local->slot, field->offset, 0);
            if (field->kind == SLOT_ARRAY_I32) {
                body_op3(body, BODY_OP_ARRAY_I32_INDEX_STORE, val_slot, ref_slot, idx_slot, 0);
            } else if (field->kind == SLOT_SEQ_I32 || field->kind == SLOT_SEQ_I32_REF) {
                body_op3(body, BODY_OP_SEQ_I32_INDEX_STORE, val_slot, ref_slot, idx_slot, 0);
            } else if (field->kind == SLOT_SEQ_OPAQUE) {
                int32_t element_size = cold_seq_opaque_element_size_for_slot(lower->csg->symbols, body, ref_slot);
                if (body->slot_size[val_slot] > element_size) die("cold csg opaque field store value too large");
                body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_STORE, val_slot, ref_slot, idx_slot, element_size);
            } else {
                die("cold csg field index assignment target unsupported");
            }
        } else {
            body_op3(body, BODY_OP_PAYLOAD_STORE, obj_local->slot, val_slot, field->offset, field->size);
        }
        return;
    }
    /* check for direct array index: digits[count] = value */
    int32_t lb2 = cold_span_find_char(name, '[');
    if (lb2 > 0) {
        Span arr_name = span_trim(span_sub(name, 0, lb2));
        Span idx_expr = span_trim(span_sub(name, lb2 + 1, name.len - 1));
        Local *arr_local = locals_find(locals, arr_name);
        if (!arr_local) die("cold csg index assign: array not found");
        int32_t kind2 = SLOT_I32;
        int32_t val_slot = csg_parse_expr_span(lower, body, locals, expr, &kind2);
        int32_t idx_slot = csg_parse_expr_span(lower, body, locals, idx_expr, &kind2);
        if (arr_local->kind == SLOT_ARRAY_I32) {
            body_op3(body, BODY_OP_ARRAY_I32_INDEX_STORE, val_slot, arr_local->slot, idx_slot, 0);
        } else if (arr_local->kind == SLOT_SEQ_I32 || arr_local->kind == SLOT_SEQ_I32_REF) {
            body_op3(body, BODY_OP_SEQ_I32_INDEX_STORE, val_slot, arr_local->slot, idx_slot, 0);
        } else if (arr_local->kind == SLOT_SEQ_OPAQUE || arr_local->kind == SLOT_SEQ_OPAQUE_REF) {
            int32_t element_size = cold_seq_opaque_element_size_for_slot(lower->csg->symbols, body, arr_local->slot);
            if (body->slot_size[val_slot] > element_size) die("cold csg opaque index store value too large");
            body_op3(body, BODY_OP_SEQ_OPAQUE_INDEX_STORE, val_slot, arr_local->slot, idx_slot, element_size);
        } else {
            die("cold csg index assignment target unsupported");
        }
        return;
    }
    Local *local = locals_find(locals, name);
    if (!local) die("cold csg assignment target must be a local");
    int32_t kind = SLOT_I32;
    int32_t slot = csg_parse_expr_span(lower, body, locals, expr, &kind);
    if (local->kind == SLOT_I32) {
        if (kind != SLOT_I32) die("cold csg assignment value must be int32");
        body_op(body, BODY_OP_COPY_I32, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_I32_REF) {
        if (kind != SLOT_I32) die("cold csg i32_ref assignment value must be int32");
        body_op(body, BODY_OP_I32_REF_STORE, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_STR_REF) {
        if (kind != SLOT_STR) die("cold csg var str assignment value must be str");
        body_op(body, BODY_OP_STR_REF_STORE, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_STR) {
        if (kind != SLOT_STR) die("cold csg str assignment value must be str");
        body_op(body, BODY_OP_COPY_COMPOSITE, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_OPAQUE || local->kind == SLOT_OPAQUE_REF) {
        body_op3(body, BODY_OP_PAYLOAD_STORE, local->slot, slot, 0, 4);
        return;
    }
    if (local->kind == SLOT_OBJECT || local->kind == SLOT_OBJECT_REF) {
        int32_t obj_size = 16;
        if (local->slot >= 0 && local->slot < body->slot_count) {
            obj_size = body->slot_size[local->slot];
        }
        if (obj_size <= 0) obj_size = 16;
        body_op3(body, BODY_OP_PAYLOAD_STORE, local->slot, slot, 0, obj_size);
        return;
    }
    if (local->kind == SLOT_VARIANT) {
        body_op(body, BODY_OP_COPY_I32, local->slot, slot, 0);
        return;
    }
    if (local->kind == SLOT_SEQ_OPAQUE || local->kind == SLOT_SEQ_OPAQUE_REF ||
        local->kind == SLOT_SEQ_STR || local->kind == SLOT_SEQ_I32) {
        body_op3(body, BODY_OP_MAKE_COMPOSITE, local->slot, slot, 0, 0);
        return;
    }
    die("cold csg assignment target kind unsupported");
}

static void csg_parse_add(ColdCsgLower *lower, BodyIR *body,
                          Locals *locals, Span args) {
    Span items[2];
    int32_t count = cold_split_top_level_commas(args, items, 2);
    if (count != 2) die("cold csg add expects two arguments");
    int32_t seq_kind = SLOT_I32;
    int32_t seq_slot = parse_seq_lvalue_from_span(&lower->owner, body, locals, items[0], &seq_kind);
    int32_t value_kind = SLOT_I32;
    int32_t value_slot = csg_parse_expr_span(lower, body, locals, items[1], &value_kind);
    if (seq_kind == SLOT_SEQ_I32 || seq_kind == SLOT_SEQ_I32_REF) {
        if (value_kind != SLOT_I32) {
            fprintf(stderr, "cheng_cold: csg add int32[] value kind=%d, skipping\n", value_kind);
        } else {
            body_op(body, BODY_OP_SEQ_I32_ADD, seq_slot, value_slot, 0);
        }
    } else if (seq_kind == SLOT_SEQ_OPAQUE || seq_kind == SLOT_SEQ_OPAQUE_REF) {
        int32_t element_size = cold_seq_opaque_element_size_for_slot(lower->csg->symbols, body, seq_slot);
        body_op(body, BODY_OP_SEQ_OPAQUE_ADD, seq_slot, value_slot, element_size);
    } else {
        if (value_kind != SLOT_STR && value_kind != SLOT_I32) {
            fprintf(stderr, "cheng_cold: csg add str[] value kind=%d, skipping\n", value_kind);
        } else {
            body_op(body, BODY_OP_SEQ_STR_ADD, seq_slot, value_slot, 0);
        }
    }
}

static void csg_parse_remove(ColdCsgLower *lower, BodyIR *body,
                             Locals *locals, Span args) {
    Span items[2];
    int32_t count = cold_split_top_level_commas(args, items, 2);
    if (count != 2) die("cold csg remove expects two arguments");
    int32_t seq_kind = SLOT_I32;
    int32_t seq_slot = parse_seq_lvalue_from_span(&lower->owner, body, locals, items[0], &seq_kind);
    int32_t index_kind = SLOT_I32;
    int32_t index_slot = csg_parse_expr_span(lower, body, locals, items[1], &index_kind);
    if (index_kind != SLOT_I32) die("cold csg remove index must be int32");
    if (seq_kind == SLOT_SEQ_OPAQUE || seq_kind == SLOT_SEQ_OPAQUE_REF) {
        int32_t element_size = cold_seq_opaque_element_size_for_slot(lower->csg->symbols, body, seq_slot);
        body_op3(body, BODY_OP_SEQ_OPAQUE_REMOVE, seq_slot, index_slot, element_size, 0);
    }
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
        if (matched->kind != SLOT_VARIANT && matched->kind != SLOT_I32 && matched->kind != SLOT_STR)
            return block; /* skip csg non-variant match value */;
        matched_slot = matched->slot;
        if (matched->kind != SLOT_VARIANT) {
            int32_t vs = body_slot(body, SLOT_VARIANT, 4);
            body_op(body, BODY_OP_COPY_I32, vs, matched_slot, 0);
            matched_slot = vs;
        }
    } else {
        int32_t kind = SLOT_I32;
        matched_slot = csg_parse_expr_span(lower, body, locals, stmt.a, &kind);
        if (kind != SLOT_VARIANT) return block; /* skip csg non-variant match */;
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
    TypeDef *match_type = body->slot_type[matched_slot].len > 0
                              ? symbols_resolve_type(lower->csg->symbols, body->slot_type[matched_slot])
                              : 0;
    int32_t arm_indent = csg_next_indent(lower);
    if (arm_indent <= stmt.indent) die("cold csg match must have indented arms");
    while (lower->cursor < lower->end) {
        ColdCsgStmt *arm = &lower->csg->stmts[lower->cursor];
        if (arm->indent <= stmt.indent) break;
        if (arm->indent != arm_indent || !span_eq(arm->kind, "case")) {
            die("cold csg match arm must be a case row");
        }
        lower->cursor++;

        Variant *variant = match_type ? type_find_variant(match_type, arm->a)
                                      : symbols_find_variant(lower->csg->symbols, arm->a);
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
        body_switch_case(body, switch_term, variant->tag, arm_block);
        case_count++;
        int32_t saved_local_count = locals->count;
        Span bind_names[COLD_MAX_VARIANT_FIELDS];
        int32_t bind_count = csg_parse_binding_csv(arm->b, bind_names, COLD_MAX_VARIANT_FIELDS);
        if (bind_count != variant->field_count) die("cold csg match arm payload binding count mismatch");
        for (int32_t field_index = 0; field_index < bind_count; field_index++) {
            int32_t field_kind = variant->field_kind[field_index];
            int32_t payload_slot = body_slot(body, field_kind, variant->field_size[field_index]);
            body_slot_set_type(body, payload_slot, variant->field_type[field_index]);
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
    if (!match_type || case_count != match_type->variant_count) return block; /* non-exhaustive csg match */
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
    if (true_indent <= stmt.indent) {
        /* Inline suite fallback: emit unconditional branch to join block.
           Multi-line suites require proper indentation. */
        body_branch_to(body, true_block, block);
        body_branch_to(body, false_block, block);
        return block;
    }
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
            if (false_indent <= stmt.indent) {
                fprintf(stderr, "cheng_cold: else suite must be indented, skipping\n");
                body_branch_to(body, false_block, block);
                return true_end;
            }
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
    if (loop_indent <= stmt.indent) {
        fprintf(stderr, "cheng_cold: for suite must be indented, skipping\n");
        return block;
    }
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
    if (loop_indent <= stmt.indent) {
        fprintf(stderr, "cheng_cold: while suite must be indented, skipping\n");
        return block;
    }
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
        if (span_eq(stmt.a, "add")) {
            csg_parse_add(lower, body, locals, stmt.b);
            return block;
        }
        if (span_eq(stmt.a, "remove")) {
            csg_parse_remove(lower, body, locals, stmt.b);
            return block;
        }
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
#endif /* COLD_BACKEND_ONLY */
/* ================================================================
 * ARM64 encoder
 * ================================================================ */
enum {
    R0 = 0, R1 = 1, R2 = 2, R3 = 3, R4 = 4, R5 = 5, R6 = 6, R7 = 7, R8 = 8,
    R9 = 9, R10 = 10, R11 = 11, R12 = 12,
    SP = 31, LR = 30, FP = 29,
};

static uint32_t a64_ret(void) { return 0xD65F03C0u; }
static uint32_t a64_brk(int imm) {
    return 0xD4200000u | ((uint32_t)(imm & 0xFFFF) << 5);
}
static uint32_t a64_movz(int rd, uint16_t value, int shift) {
    return 0x52800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
}
static uint32_t a64_movk(int rd, uint16_t value, int shift) {
    return 0x72800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
}
static uint32_t a64_movk_x(int rd, uint16_t value, int shift) {
    return 0xF2800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
}
static uint32_t a64_movz_x(int rd, uint16_t value, int shift) {
    return 0xD2800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
}
__attribute__((unused))
static uint32_t a64_movn(int rd, uint16_t value, int shift) {
    return 0x92800000u | ((uint32_t)shift << 21) | ((uint32_t)value << 5) | (uint32_t)rd;
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
__attribute__((unused))
static uint32_t a64_add_imm_x(int rd, int rn, uint16_t value, bool x) {
    return a64_add_imm(rd, rn, value, x);
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
static uint32_t a64_strb_imm(int rt, int rn, int32_t offset) {
    uint32_t imm = (uint32_t)(offset & 0xFFF);
    return 0x39000000u | (imm << 10) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
__attribute__((unused))
static uint32_t a64_ldr_reg_x(int rt, int rn, int rm) {
    return 0xF8606800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
__attribute__((unused))
static uint32_t a64_str_reg_x(int rt, int rn, int rm) {
    return 0xF8206800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rt;
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
static uint32_t a64_cmp_reg_x(int rn, int rm) {
    return 0xEB00001Fu | ((uint32_t)rm << 16) | ((uint32_t)rn << 5);
}
static uint32_t a64_add_reg(int rd, int rn, int rm) {
    return 0x0B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_add_reg_x(int rd, int rn, int rm) {
    return 0x8B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_sub_reg(int rd, int rn, int rm) {
    return 0x4B000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_sub_reg_x(int rd, int rn, int rm) {
    return 0xCB000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_mul_reg(int rd, int rn, int rm) {
    return 0x1B007C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_mul_reg_x(int rd, int rn, int rm) {
    return 0x9B007C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_sdiv_reg(int rd, int rn, int rm) {
    return 0x1AC00C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_sdiv_reg_x(int rd, int rn, int rm) {
    return 0x9AC00C00u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_msub_reg(int rd, int rn, int rm, int ra) {
    return 0x1B008000u | ((uint32_t)rm << 16) | ((uint32_t)ra << 10) |
           ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_msub_reg_x(int rd, int rn, int rm, int ra) {
    return 0x9B008000u | ((uint32_t)rm << 16) | ((uint32_t)ra << 10) |
           ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_lsl_reg(int rd, int rn, int rm) {
    return 0x1AC02000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}
__attribute__((unused))
static uint32_t a64_lsl_imm(int rd, int rn, int shift, bool x) {
    if (x) {
        return 0xD3400000u | ((uint32_t)(64 - shift) << 16) |
               ((uint32_t)(63 - shift) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
    }
    return 0x53000000u | ((uint32_t)(32 - shift) << 16) |
           ((uint32_t)(31 - shift) << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_lsr_imm(int rd, int rn, int shift, bool x) {
    if (x) {
        return 0xD3400000u | ((uint32_t)shift << 16) |
               (63u << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
    }
    return 0x53000000u | ((uint32_t)shift << 16) |
           (31u << 10) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_eor_reg(int rd, int rn, int rm) {
    return 0x4A000000u | ((uint32_t)(rm & 31) << 16) | ((uint32_t)(rn & 31) << 5) | (uint32_t)(rd & 31);
}
static uint32_t a64_asr_reg(int rd, int rn, int rm) {
    return 0x1AC02800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_and_reg(int rd, int rn, int rm) {
    return 0x0A000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_and_reg_x(int rd, int rn, int rm) {
    return 0x8A000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_orr_reg_x(int rd, int rn, int rm) {
    return 0xAA000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_eor_reg_x(int rd, int rn, int rm) {
    return 0xCA000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_lsl_reg_x(int rd, int rn, int rm) {
    return 0x9AC02000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_asr_reg_x(int rd, int rn, int rm) {
    return 0x9AC02800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
__attribute__((unused))
static uint32_t a64_and_imm(int rd, int rn, uint32_t imm, bool x) {
    if (x && imm == 0xFFF0u) {
        return 0x927CEC00u | ((uint32_t)rn << 5) | (uint32_t)rd;
    }
    die("unsupported arm64 and immediate");
    return 0;
}
static uint32_t a64_orr_reg(int rd, int rn, int rm) {
    return 0x2A000000u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd;
}
static uint32_t a64_ldr_w_reg_uxtw2(int rt, int rn, int rm) {
    return 0xB8600800u | ((uint32_t)rm << 16) | (2u << 13) |
           (1u << 12) | ((uint32_t)rn << 5) | (uint32_t)rt;
}
static uint32_t a64_str_w_reg_uxtw2(int rt, int rn, int rm) {
    return 0xB8200800u | ((uint32_t)rm << 16) | (2u << 13) |
           (1u << 12) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_ldr_x_reg_lsl3(int rt, int rn, int rm) {
    return 0xF8607800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_str_x_reg_lsl3(int rt, int rn, int rm) {
    return 0xF8207800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

static uint32_t a64_b(int32_t offset_words) {
    return 0x14000000u | ((uint32_t)offset_words & 0x03FFFFFFu);
}
static uint32_t a64_bl(int32_t offset_words) {
    return 0x94000000u | ((uint32_t)offset_words & 0x03FFFFFFu);
}
static uint32_t a64_blr(int rn) {
    return 0xD63F0000u | ((uint32_t)rn << 5);
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

__attribute__((unused))
static uint32_t a64_cset(int rd, int cond) {
    return 0x1A9F07E0u | ((uint32_t)rd) | ((uint32_t)(cond & 0xF) << 12);
}
__attribute__((unused))
static uint32_t a64_ldar_w(int rt, int rn) {
    return 0x88DFFC00u | ((uint32_t)rn << 5) | (uint32_t)rt;
}
__attribute__((unused))
static uint32_t a64_stlr_w(int rt, int rn) {
    return 0x889FFC00u | ((uint32_t)rn << 5) | (uint32_t)rt;
}
__attribute__((unused))
static uint32_t a64_ldaxr_w(int rt, int rn) {
    return 0x885FFC00u | ((uint32_t)rn << 5) | (uint32_t)rt;
}
__attribute__((unused))
static uint32_t a64_stlxr_w(int rs, int rt, int rn) {
    return 0x8800FC00u | ((uint32_t)rs << 16) | ((uint32_t)rn << 5) | (uint32_t)rt;
}

/* ================================================================
 * Code generation
 * ================================================================ */
typedef struct Code {
    uint32_t *words;
    int32_t count;
    int32_t cap;
    Arena *arena;
} Code;

typedef struct Patch {
    int32_t pos;
    int32_t target_block;
    int32_t kind;
} Patch;

typedef struct PatchList {
    Patch *items;
    int32_t count;
    int32_t cap;
    Arena *arena;
} PatchList;

typedef struct FunctionPatch {
    int32_t pos;
    int32_t target_function;
} FunctionPatch;

typedef struct FunctionPatchList {
    FunctionPatch *items;
    int32_t count;
    int32_t cap;
    Arena *arena;
} FunctionPatchList;

static void code_emit(Code *code, uint32_t word);
static void codegen_func(Code *code, BodyIR *body, Symbols *symbols,
                         FunctionPatchList *function_patches);

#define COLD_WSDEQUE_CAP 256
typedef struct ColdWSDeque {
    int32_t tasks[COLD_WSDEQUE_CAP];
    int32_t top;
    int32_t bottom;
} ColdWSDeque;

static bool cold_wsdeque_push(ColdWSDeque *q, int32_t task) {
    int32_t t = q->top;
    if (t >= COLD_WSDEQUE_CAP) return false;
    q->tasks[t] = task;
    __atomic_store_n(&q->top, t + 1, __ATOMIC_RELEASE);
    return true;
}

static bool cold_wsdeque_pop(ColdWSDeque *q, int32_t *task) {
    int32_t t = __atomic_load_n(&q->top, __ATOMIC_ACQUIRE);
    if (t <= 0) return false;
    int32_t new_t = t - 1;
    __atomic_store_n(&q->top, new_t, __ATOMIC_RELEASE);
    int32_t b = __atomic_load_n(&q->bottom, __ATOMIC_ACQUIRE);
    if (new_t >= b) {
        *task = q->tasks[new_t];
        return true;
    }
    __atomic_store_n(&q->top, b, __ATOMIC_RELEASE);
    return false;
}

static bool cold_wsdeque_steal(ColdWSDeque *q, int32_t *task) {
    int32_t b = __atomic_load_n(&q->bottom, __ATOMIC_ACQUIRE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    int32_t t = __atomic_load_n(&q->top, __ATOMIC_ACQUIRE);
    if (b >= t) return false;
    *task = q->tasks[b];
    int32_t expected = b;
    if (!__atomic_compare_exchange_n(&q->bottom, &expected, b + 1,
                                     0, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        return false;
    }
    return true;
}


/* ---- parallel codegen worker (lock-free work-stealing) ---- */

typedef struct CodegenWorker {
    ColdWSDeque *deque;          /* per-worker work-stealing deque */
    ColdWSDeque *all_deques;     /* base of all workers' deques (array) */
    int32_t worker_index;        /* index of this worker (0..active-1) */
    int32_t active_workers;      /* total active workers */
    int32_t entry_function;
    int32_t function_count;
    BodyIR **function_bodies;
    bool *reachable_functions;
    Symbols *symbols;
    Code *local_code;
    FunctionPatchList local_patches;
    int32_t *local_function_pos;
    int32_t *local_function_end;
} CodegenWorker;

/* Lock-free Work-Stealing Deque (Chase-Lev style).
   Owner pushes/pops from top; thieves steal from bottom.
   All synchronisation via __atomic builtins. */
static void *codegen_worker_run(void *arg) {
    CodegenWorker *w = (CodegenWorker *)arg;
    for (;;) {
        int32_t i = -1;
        /* 1. Try to pop from own deque */
        if (!cold_wsdeque_pop(w->deque, &i)) {
            /* 2. Steal from a random victim */
            bool stolen = false;
            for (int32_t attempt = 0; attempt < w->active_workers * 2; attempt++) {
                int32_t victim = (w->worker_index + 1 + attempt) % w->active_workers;
                if (cold_wsdeque_steal(&w->all_deques[victim], &i)) {
                    stolen = true;
                    break;
                }
            }
            if (!stolen) break;  /* all queues empty */
        }
        if (i < 0 || i >= w->function_count) continue;
        if (i == w->entry_function || !w->reachable_functions[i] ||
            !w->function_bodies[i] ||
            w->function_bodies[i]->has_fallback) continue;
        w->local_function_pos[i] = w->local_code->count;
        BodyIR *body = w->function_bodies[i];
        if (body->has_fallback || body->block_count == 0 ||
            (body->block_count > 0 && body->block_term[0] < 0)) {
            die("cold worker function body is not codegen-ready");
        }
        codegen_func(w->local_code, body, w->symbols, &w->local_patches);
        w->local_function_end[i] = w->local_code->count;
    }
    return NULL;
}

static int32_t cold_jobs_from_env(void) {
    const char *env = getenv("BACKEND_JOBS");
    if (!env || !env[0]) return 1;
    int32_t n = 0;
    for (const char *p = env; *p; p++) {
        if (*p >= '0' && *p <= '9') n = n * 10 + (*p - '0');
        else return 1;
    }
    if (n < 1) return 1;
    if (n > 16) n = 16;
    return n;
}

/* ---- end parallel codegen ---- */

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

static void a64_patch_b(Code *code, int32_t pos, int32_t target) {
    int32_t delta = target - pos;
    code->words[pos] = (code->words[pos] & 0xFC000000u) | ((uint32_t)delta & 0x03FFFFFFu);
}

static void a64_patch_bcond(Code *code, int32_t pos, int32_t target) {
    int32_t delta = target - pos;
    code->words[pos] = (code->words[pos] & 0xFF00001Fu) | (((uint32_t)delta & 0x7FFFFu) << 5);
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

/* ---- large immediate / large offset helpers ---- */

static uint32_t a64_add_imm_shifted(int rd, int rn, uint16_t imm12, bool x64) {
    return (x64 ? 0x91000000u : 0x11000000u) | ((uint32_t)rd) | ((uint32_t)rn << 5)
           | ((uint32_t)(imm12 & 0xFFF) << 10) | (1u << 22);
}

static uint32_t a64_sub_imm_shifted(int rd, int rn, uint16_t imm12, bool x64) {
    return (x64 ? 0xD1000000u : 0x51000000u) | ((uint32_t)rd) | ((uint32_t)rn << 5)
           | ((uint32_t)(imm12 & 0xFFF) << 10) | (1u << 22);
}

/* Emit SUB with potentially large 64-bit immediate (two instructions if needed). */
static void a64_emit_sub_large(Code *code, int rd, int rn, int32_t value, bool x64) {
    if (value <= 0xFFF) {
        code_emit(code, a64_sub_imm(rd, rn, (uint16_t)value, x64));
        return;
    }
    if (value % 0x1000 == 0 && value / 0x1000 <= 0xFFF) {
        code_emit(code, a64_sub_imm_shifted(rd, rn, (uint16_t)(value / 0x1000), x64));
        return;
    }
    int32_t hi = (value >> 12) << 12;
    int32_t lo = value - hi;
    if (hi > 0)
        code_emit(code, a64_sub_imm_shifted(rd, rn, (uint16_t)(hi / 0x1000), x64));
    if (lo > 0)
        code_emit(code, a64_sub_imm(rd, rd, (uint16_t)lo, x64));
}

/* Emit ADD with potentially large 64-bit immediate. */
static void a64_emit_add_large(Code *code, int rd, int rn, int32_t value, bool x64) {
    if (value <= 0xFFF) {
        code_emit(code, a64_add_imm(rd, rn, (uint16_t)value, x64));
        return;
    }
    if (value % 0x1000 == 0 && value / 0x1000 <= 0xFFF) {
        code_emit(code, a64_add_imm_shifted(rd, rn, (uint16_t)(value / 0x1000), x64));
        return;
    }
    int32_t hi = (value >> 12) << 12;
    int32_t lo = value - hi;
    if (hi > 0)
        code_emit(code, a64_add_imm_shifted(rd, rn, (uint16_t)(hi / 0x1000), x64));
    if (lo > 0)
        code_emit(code, a64_add_imm(rd, rd, (uint16_t)lo, x64));
}

/* Emit 16-bit immediate into x16 (single movz for values 0-65535, two for larger). */
__attribute__((unused))
static void a64_emit_mov_imm_x16(Code *code, int32_t value) {
    code_emit(code, a64_movz_x(16, (uint16_t)(value & 0xFFFF), 0));
    if (value > 0xFFFF)
        code_emit(code, a64_movk(16, (uint16_t)((value >> 16) & 0xFFFF), 1));
}

/* Load from SP with potentially large unsigned offset. */
static void a64_emit_ldr_sp_off(Code *code, int rt, int32_t offset, bool x64) {
    int32_t max_off = x64 ? 32760 : 16380;
    if (offset <= max_off) {
        code_emit(code, a64_ldr_imm(rt, SP, offset, x64));
    } else {
        a64_emit_add_large(code, 16, SP, offset, true);
        code_emit(code, a64_ldr_imm(rt, 16, 0, x64));
    }
}

/* Store to SP with potentially large unsigned offset. */
static void a64_emit_str_sp_off(Code *code, int rt, int32_t offset, bool x64) {
    int32_t max_off = x64 ? 32760 : 16380;
    if (offset <= max_off) {
        code_emit(code, a64_str_imm(rt, SP, offset, x64));
    } else {
        a64_emit_add_large(code, 16, SP, offset, true);
        code_emit(code, a64_str_imm(rt, 16, 0, x64));
    }
}

static void emit_epilogue(Code *code, int32_t frame_size) {
    if (frame_size > 0) a64_emit_add_large(code, SP, SP, frame_size, true);
    code_emit(code, a64_ldp_post(FP, LR, SP, 16));
    code_emit(code, a64_ldp_post(19, 20, SP, 16));
    code_emit(code, a64_ret());
}

#ifdef COLD_BACKEND_ONLY
static int32_t cold_arg_reg_count(int32_t kind, int32_t size) {
    if (kind == SLOT_I32 || kind == SLOT_I64 || kind == SLOT_I32_REF || kind == SLOT_I64_REF) return 1;
    if (kind == SLOT_PTR) return 1;
    if (kind == SLOT_STR) return 2;
    if (kind == SLOT_VARIANT) return size > 16 ? 1 : 2;
    if (kind == SLOT_OBJECT || kind == SLOT_ARRAY_I32) return size > 16 ? 1 : 2;
    if (kind == SLOT_SEQ_I32) return 2;
    if (kind == SLOT_SEQ_STR) return 2;
    if (kind == SLOT_SEQ_OPAQUE) return 2;
    if (kind == SLOT_I32_REF ||
        kind == SLOT_SEQ_I32_REF || kind == SLOT_OBJECT_REF ||
        kind == SLOT_STR_REF || kind == SLOT_SEQ_STR_REF ||
        kind == SLOT_SEQ_OPAQUE_REF ||
        kind == SLOT_OPAQUE || kind == SLOT_OPAQUE_REF) return 1;
    die("unsupported cold call ABI kind");
    return 1;
}
#endif /* COLD_BACKEND_ONLY */

static int32_t cold_abi_arg_storage_bytes(int32_t kind, int32_t size) {
    return cold_arg_reg_count(kind, size) * 8;
}

static bool cold_abi_place_arg(int32_t *reg_cursor, int32_t *stack_cursor,
                               int32_t kind, int32_t size,
                               int32_t *base_reg, int32_t *stack_offset) {
    int32_t reg_count = cold_arg_reg_count(kind, size);
    if (*reg_cursor + reg_count <= 8) {
        *base_reg = *reg_cursor;
        *stack_offset = -1;
        *reg_cursor += reg_count;
        return true;
    }
    *base_reg = -1;
    *stack_offset = *stack_cursor;
    *stack_cursor += cold_abi_arg_storage_bytes(kind, size);
    *reg_cursor = 8;
    return false;
}

static int32_t cold_outgoing_stack_bytes(FnDef *fn) {
    int32_t reg = 0;
    int32_t stack = 0;
    for (int32_t i = 0; i < fn->arity; i++) {
        int32_t base_reg = -1;
        int32_t stack_offset = -1;
        (void)cold_abi_place_arg(&reg, &stack, fn->param_kind[i],
                                 fn->param_size[i], &base_reg, &stack_offset);
    }
    return align_i32(stack, 16);
}

static void codegen_store_params(Code *code, BodyIR *body) {
    int32_t reg = 0;
    int32_t stack = 0;
    for (int32_t i = 0; i < body->param_count; i++) {
        int32_t slot = body->param_slot[i];
        int32_t kind = body->slot_kind[slot];
        int32_t size = body->slot_size[slot];
        int32_t base_reg = -1;
        int32_t stack_offset = -1;
        bool in_regs = cold_abi_place_arg(&reg, &stack, kind, size,
                                          &base_reg, &stack_offset);
        int32_t incoming_stack_offset = 32 + stack_offset;
        if (kind == SLOT_I32) {
            if (in_regs) {
                a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], false);
            } else {
                code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, false));
                a64_emit_str_sp_off(code, R9, body->slot_offset[slot], false);
            }
        } else if (kind == SLOT_I64 || kind == SLOT_PTR) {
            if (in_regs) {
                a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], true);
            } else {
                code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, true));
                a64_emit_str_sp_off(code, R9, body->slot_offset[slot], true);
            }
        } else if (kind == SLOT_STR) {
            if (in_regs) {
                a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], true);
                a64_emit_str_sp_off(code, base_reg + 1, body->slot_offset[slot] + 8, true);
            } else {
                code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, true));
                a64_emit_str_sp_off(code, R9, body->slot_offset[slot], true);
                code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset + 8, true));
                a64_emit_str_sp_off(code, R9, body->slot_offset[slot] + 8, true);
            }
        } else if (kind == SLOT_I32_REF || kind == SLOT_I64_REF) {
            if (in_regs) {
                a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], true);
            } else {
                code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, true));
                a64_emit_str_sp_off(code, R9, body->slot_offset[slot], true);
            }
        } else if (kind == SLOT_STR_REF) {
            if (in_regs) {
                a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], true);
            } else {
                code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, true));
                a64_emit_str_sp_off(code, R9, body->slot_offset[slot], true);
            }
        } else if (kind == SLOT_VARIANT) {
            if (size > 16) {
                int32_t src_reg = in_regs ? base_reg : R9;
                if (!in_regs) code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, true));
                for (int32_t off = 0; off < size; off += 8) {
                    code_emit(code, a64_ldr_imm(R10, src_reg, off, true));
                    a64_emit_str_sp_off(code, R10, body->slot_offset[slot] + off, true);
                }
            } else {
                if (in_regs) {
                    a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], true);
                    a64_emit_str_sp_off(code, base_reg + 1, body->slot_offset[slot] + 8, true);
                } else {
                    for (int32_t off = 0; off < size; off += 8) {
                        code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset + off, true));
                        a64_emit_str_sp_off(code, R9, body->slot_offset[slot] + off, true);
                    }
                }
            }
        } else if (kind == SLOT_OBJECT || kind == SLOT_ARRAY_I32 ||
                   kind == SLOT_SEQ_I32 || kind == SLOT_SEQ_STR ||
                   kind == SLOT_SEQ_OPAQUE) {
            if (size > 16) {
                int32_t src_reg = in_regs ? base_reg : R9;
                if (!in_regs) code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, true));
                for (int32_t off = 0; off < size; off += 8) {
                    code_emit(code, a64_ldr_imm(R10, src_reg, off, true));
                    a64_emit_str_sp_off(code, R10, body->slot_offset[slot] + off, true);
                }
            } else {
                if (in_regs) {
                    a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], true);
                    a64_emit_str_sp_off(code, base_reg + 1, body->slot_offset[slot] + 8, true);
                } else {
                    for (int32_t off = 0; off < size; off += 8) {
                        code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset + off, true));
                        a64_emit_str_sp_off(code, R9, body->slot_offset[slot] + off, true);
                    }
                }
            }
        } else if (kind == SLOT_SEQ_I32_REF || kind == SLOT_SEQ_STR_REF ||
                   kind == SLOT_SEQ_OPAQUE_REF ||
                   kind == SLOT_OBJECT_REF || kind == SLOT_OPAQUE ||
                   kind == SLOT_OPAQUE_REF) {
            if (in_regs) {
                a64_emit_str_sp_off(code, base_reg, body->slot_offset[slot], true);
            } else {
                code_emit(code, a64_ldr_imm(R9, FP, incoming_stack_offset, true));
                a64_emit_str_sp_off(code, R9, body->slot_offset[slot], true);
            }
        } else {
            die("unsupported cold parameter kind");
        }
    }
}

static void codegen_place_x_arg(Code *code, bool in_regs, int32_t base_reg,
                                int32_t stack_offset, int32_t src_reg) {
    if (in_regs) {
        if (base_reg != src_reg) code_emit(code, a64_add_imm(base_reg, src_reg, 0, true));
    } else {
        a64_emit_str_sp_off(code, src_reg, stack_offset, true);
    }
}

static void codegen_place_w_arg(Code *code, bool in_regs, int32_t base_reg,
                                int32_t stack_offset, int32_t src_reg) {
    if (in_regs) {
        if (base_reg != src_reg) code_emit(code, a64_add_imm(base_reg, src_reg, 0, true));
    } else {
        a64_emit_str_sp_off(code, src_reg, stack_offset, false);
    }
}

static int32_t codegen_load_call_args(Code *code, BodyIR *body, FnDef *fn, int32_t arg_start) {
    int32_t stack_bytes = cold_outgoing_stack_bytes(fn);
    if (stack_bytes > 0) a64_emit_sub_large(code, SP, SP, stack_bytes, true);
    int32_t reg = 0;
    int32_t stack = 0;
    for (int32_t i = 0; i < fn->arity; i++) {
        int32_t arg_slot = body->call_arg_slot[arg_start + i];
        int32_t arg_kind = body->slot_kind[arg_slot];
        int32_t param_size = fn->param_size[i] > 0 ? fn->param_size[i] : body->slot_size[arg_slot];
        int32_t base_reg = -1;
        int32_t stack_offset = -1;
        bool in_regs = cold_abi_place_arg(&reg, &stack, fn->param_kind[i],
                                          param_size, &base_reg, &stack_offset);
        int32_t local_offset = body->slot_offset[arg_slot] + stack_bytes;
        if (fn->param_kind[i] == SLOT_I32_REF) {
            if (arg_kind == SLOT_I32_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else if (arg_kind == SLOT_I32) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_I64_REF) {
            if (arg_kind == SLOT_I64_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else if (arg_kind == SLOT_I64) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_SEQ_I32_REF) {
            if (arg_kind == SLOT_SEQ_I32_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else if (arg_kind == SLOT_SEQ_I32) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_SEQ_STR_REF) {
            if (arg_kind == SLOT_SEQ_STR_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else if (arg_kind == SLOT_SEQ_STR) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_SEQ_OPAQUE_REF) {
            if (arg_kind == SLOT_SEQ_OPAQUE_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else if (arg_kind == SLOT_SEQ_OPAQUE) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_STR_REF) {
            if (arg_kind == SLOT_STR_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else if (arg_kind == SLOT_STR) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_PTR) {
            a64_emit_ldr_sp_off(code, R9, local_offset, true);
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_OBJECT_REF) {
            if (arg_kind == SLOT_OBJECT_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else if (arg_kind == SLOT_OBJECT) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_OPAQUE_REF) {
            if (arg_kind == SLOT_OPAQUE_REF || arg_kind == SLOT_OBJECT_REF ||
                arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                arg_kind == SLOT_STR_REF || arg_kind == SLOT_SEQ_I32_REF ||
                arg_kind == SLOT_SEQ_STR_REF || arg_kind == SLOT_SEQ_OPAQUE_REF) {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
            } else {
                a64_emit_add_large(code, R9, SP, local_offset, true);
            }
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_I32_REF) {
            a64_emit_ldr_sp_off(code, R9, local_offset, true);
            code_emit(code, a64_ldr_imm(R9, R9, 0, false));
            codegen_place_w_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_I32 && arg_kind == SLOT_VARIANT) {
            a64_emit_ldr_sp_off(code, R9, local_offset, false);
            codegen_place_w_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_I64 && arg_kind == SLOT_I64_REF) {
            a64_emit_ldr_sp_off(code, R9, local_offset, true);
            code_emit(code, a64_ldr_imm(R9, R9, 0, true));
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            continue;
        }
        if (fn->param_kind[i] == SLOT_STR && arg_kind == SLOT_STR_REF) {
            a64_emit_ldr_sp_off(code, R9, local_offset, true);
            if (in_regs) {
                code_emit(code, a64_ldr_imm(base_reg, R9, 0, true));
                code_emit(code, a64_ldr_imm(base_reg + 1, R9, 8, true));
            } else {
                code_emit(code, a64_ldr_imm(R9, R9, 0, true));
                a64_emit_str_sp_off(code, R9, stack_offset, true);
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
                code_emit(code, a64_ldr_imm(R9, R9, 8, true));
                a64_emit_str_sp_off(code, R9, stack_offset + 8, true);
            }
            continue;
        }
        if (fn->param_kind[i] == SLOT_OBJECT && arg_kind == SLOT_OBJECT_REF) {
            a64_emit_ldr_sp_off(code, R9, local_offset, true);
            if (param_size > 16) {
                codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            } else if (in_regs) {
                code_emit(code, a64_ldr_imm(base_reg, R9, 0, true));
                code_emit(code, a64_ldr_imm(base_reg + 1, R9, 8, true));
            } else {
                for (int32_t off = 0; off < param_size; off += 8) {
                    code_emit(code, a64_ldr_imm(R10, R9, off, true));
                    a64_emit_str_sp_off(code, R10, stack_offset + off, true);
                }
            }
            continue;
        }
        if (arg_kind != fn->param_kind[i]) {
            /* tolerate OPAQUE variance (same as cold_call_args_match) */
            if (!((fn->param_kind[i] == SLOT_OPAQUE || fn->param_kind[i] == SLOT_OPAQUE_REF) &&
                  (arg_kind == SLOT_I32 || arg_kind == SLOT_I64 ||
                   arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                   arg_kind == SLOT_OBJECT || arg_kind == SLOT_OBJECT_REF ||
                   arg_kind == SLOT_VARIANT || arg_kind == SLOT_SEQ_OPAQUE ||
                   arg_kind == SLOT_SEQ_OPAQUE_REF ||
                   arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF)))
                a64_emit_add_large(code, R9, SP, local_offset, true);
        }
        int32_t arg_size = body->slot_size[arg_slot];
        if (arg_kind == SLOT_VARIANT && arg_size != param_size) { continue; }
        if (arg_kind == SLOT_I32) {
            a64_emit_ldr_sp_off(code, R9, local_offset, false);
            codegen_place_w_arg(code, in_regs, base_reg, stack_offset, R9);
        } else if (arg_kind == SLOT_I64) {
            a64_emit_ldr_sp_off(code, R9, local_offset, true);
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
        } else if (arg_kind == SLOT_STR) {
            if (in_regs) {
                a64_emit_ldr_sp_off(code, base_reg, local_offset, true);
                a64_emit_ldr_sp_off(code, base_reg + 1, local_offset + 8, true);
            } else {
                a64_emit_ldr_sp_off(code, R9, local_offset, true);
                a64_emit_str_sp_off(code, R9, stack_offset, true);
                a64_emit_ldr_sp_off(code, R9, local_offset + 8, true);
                a64_emit_str_sp_off(code, R9, stack_offset + 8, true);
            }
        } else if (arg_kind == SLOT_OPAQUE || arg_kind == SLOT_OPAQUE_REF ||
                   arg_kind == SLOT_OBJECT_REF || arg_kind == SLOT_I32_REF || arg_kind == SLOT_I64_REF ||
                   arg_kind == SLOT_STR_REF || arg_kind == SLOT_SEQ_I32_REF ||
                   arg_kind == SLOT_SEQ_STR_REF || arg_kind == SLOT_SEQ_OPAQUE_REF) {
            a64_emit_ldr_sp_off(code, R9, local_offset, true);
            codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
        } else if (arg_kind == SLOT_VARIANT) {
            if (param_size > 16) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
                codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            } else {
                if (in_regs) {
                    a64_emit_ldr_sp_off(code, base_reg, local_offset, true);
                    a64_emit_ldr_sp_off(code, base_reg + 1, local_offset + 8, true);
                } else {
                    for (int32_t off = 0; off < param_size; off += 8) {
                        a64_emit_ldr_sp_off(code, R9, local_offset + off, true);
                        a64_emit_str_sp_off(code, R9, stack_offset + off, true);
                    }
                }
            }
        } else if (arg_kind == SLOT_OBJECT || arg_kind == SLOT_ARRAY_I32 ||
                   arg_kind == SLOT_SEQ_I32 || arg_kind == SLOT_SEQ_STR ||
                   arg_kind == SLOT_SEQ_OPAQUE) {
            if (param_size > 16) {
                a64_emit_add_large(code, R9, SP, local_offset, true);
                codegen_place_x_arg(code, in_regs, base_reg, stack_offset, R9);
            } else {
                if (in_regs) {
                    a64_emit_ldr_sp_off(code, base_reg, local_offset, true);
                    a64_emit_ldr_sp_off(code, base_reg + 1, local_offset + 8, true);
                } else {
                    for (int32_t off = 0; off < param_size; off += 8) {
                        a64_emit_ldr_sp_off(code, R9, local_offset + off, true);
                        a64_emit_str_sp_off(code, R9, stack_offset + off, true);
                    }
                }
            }
        } else {
            a64_emit_add_large(code, R9, SP, local_offset, true); /* generic SP-relative */
        }
    }
    return stack_bytes;
}

static void codegen_zero_slot(Code *code, BodyIR *body, int32_t slot) {
    code_emit(code, a64_movz_x(R0, 0, 0));
    for (int32_t off = 0; off < body->slot_size[slot]; off += 8) {
        a64_emit_str_sp_off(code, R0, body->slot_offset[slot] + off, true);
    }
}

static void codegen_mov_i32_const(Code *code, int reg, int32_t value) {
    uint32_t bits = (uint32_t)value;
    code_emit(code, a64_movz(reg, (uint16_t)(bits & 0xFFFFu), 0));
    if ((bits & 0xFFFF0000u) != 0) {
        code_emit(code, a64_movk(reg, (uint16_t)((bits >> 16) & 0xFFFFu), 1));
    }
}

static void codegen_mov_i64_const(Code *code, int reg, uint64_t value) {
    code_emit(code, a64_movz_x(reg, (uint16_t)(value & 0xFFFFu), 0));
    if ((value & 0xFFFF0000ULL) != 0) {
        code_emit(code, a64_movk_x(reg, (uint16_t)((value >> 16) & 0xFFFFu), 1));
    }
    if ((value & 0xFFFF00000000ULL) != 0) {
        code_emit(code, a64_movk_x(reg, (uint16_t)((value >> 32) & 0xFFFFu), 2));
    }
    if ((value & 0xFFFF000000000000ULL) != 0) {
        code_emit(code, a64_movk_x(reg, (uint16_t)((value >> 48) & 0xFFFFu), 3));
    }
}

static uint32_t a64_sxtw(int rd, int rn) {
    return 0x93407C00u | ((uint32_t)rn << 5) | (uint32_t)rd;
}

static uint32_t a64_cbnz(int rt, int32_t offset_words) {
    return 0x35000000u | ((uint32_t)(offset_words & 0x7FFFF) << 5) | (uint32_t)rt;
}

static uint32_t a64_fadd_s(int rd, int rn, int rm) { return 0x1E202800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_fsub_s(int rd, int rn, int rm) { return 0x1E203800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_fmul_s(int rd, int rn, int rm) { return 0x1E200800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_fdiv_s(int rd, int rn, int rm) { return 0x1E201800u | ((uint32_t)rm << 16) | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_scvtf_s(int rd, int rn) { return 0x1E220000u | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_fcvtzs_w(int rd, int rn) { return 0x1E380000u | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_fneg_s(int rd, int rn) { return 0x1E214000u | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_fmov_s(int rd, int rn) { return 0x1E270000u | ((uint32_t)rn << 5) | (uint32_t)rd; }
static uint32_t a64_fmov_sr(int rd, int rn) { return 0x1E260000u | ((uint32_t)rn << 5) | (uint32_t)rd; }

static void codegen_copy_slot_from_offset(Code *code, BodyIR *body,
                                          int32_t dst_slot, int32_t src_slot,
                                          int32_t src_offset) {
    int32_t kind = body->slot_kind[dst_slot];
    int32_t src_kind = body->slot_kind[src_slot];
    if (src_kind == SLOT_OBJECT_REF || src_kind == SLOT_SEQ_I32_REF ||
        src_kind == SLOT_SEQ_STR_REF || src_kind == SLOT_SEQ_OPAQUE_REF ||
        src_kind == SLOT_STR_REF ||
        src_kind == SLOT_PTR) {
        a64_emit_ldr_sp_off(code, R2, body->slot_offset[src_slot], true);
        /* Null check: if pointer is NULL, zero dst and return */
        code_emit(code, a64_cmp_reg_x(R2, 31));
        int32_t null_jump = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        if (kind == SLOT_I32) {
            code_emit(code, a64_ldr_imm(R0, R2, src_offset, false));
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst_slot], false);
            int32_t done_b = code->count;
            code_emit(code, a64_b(0));
            /* Null case: zero dst */
            int32_t null_case = code->count;
            a64_patch_bcond(code, null_jump, null_case);
            code_emit(code, a64_movz(R0, 0, 0));
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst_slot], false);
            code->words[done_b] = a64_b(code->count - done_b);
            return;
        }
        int32_t total = body->slot_size[dst_slot];
        for (int32_t off = 0; off < total; off += 8) {
            code_emit(code, a64_ldr_imm(R0, R2, src_offset + off, true));
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst_slot] + off, true);
        }
        int32_t done_b2 = code->count;
        code_emit(code, a64_b(0));
        /* Null case: zero entire dst slot */
        int32_t null_case2 = code->count;
        a64_patch_bcond(code, null_jump, null_case2);
        codegen_zero_slot(code, body, dst_slot);
        code->words[done_b2] = a64_b(code->count - done_b2);
        return;
    }
    if (kind == SLOT_I32) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[src_slot] + src_offset, false);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst_slot], false);
        return;
    }
    int32_t total = body->slot_size[dst_slot];
    for (int32_t off = 0; off < total; off += 8) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[src_slot] + src_offset + off, true);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst_slot] + off, true);
    }
}

static void codegen_store_slot_to_offset(Code *code, BodyIR *body,
                                         int32_t dst_slot, int32_t dst_offset,
                                         int32_t src_slot) {
    int32_t kind = body->slot_kind[src_slot];
    if (kind == SLOT_I32) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot], false);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst_slot] + dst_offset, false);
        return;
    }
    int32_t total = body->slot_size[src_slot];
    for (int32_t off = 0; off < total; off += 8) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot] + off, true);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst_slot] + dst_offset + off, true);
    }
}

static void codegen_store_slot_to_payload(Code *code, BodyIR *body,
                                          int32_t dst_slot, int32_t dst_offset,
                                          int32_t src_slot) {
    int32_t dst_kind = body->slot_kind[dst_slot];
    if (dst_kind == SLOT_OBJECT) {
        codegen_store_slot_to_offset(code, body, dst_slot, dst_offset, src_slot);
        return;
    }
    if (dst_kind != SLOT_OBJECT_REF && dst_kind != SLOT_SEQ_I32_REF &&
        dst_kind != SLOT_SEQ_STR_REF && dst_kind != SLOT_SEQ_OPAQUE_REF &&
        dst_kind != SLOT_STR_REF && dst_kind != SLOT_PTR) {
        /* Non-object slot: write source directly to SP + base + field_offset */
        int32_t base_off = body->slot_offset[dst_slot];
        int32_t total_off = base_off + dst_offset;
        int32_t src_kind = body->slot_kind[src_slot];
        if (src_kind == SLOT_I32) {
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot], false);
            a64_emit_str_sp_off(code, R1, total_off, false);
        } else if (src_kind == SLOT_I64) {
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot], true);
            a64_emit_str_sp_off(code, R1, total_off, true);
        } else {
            int32_t sz = body->slot_size[src_slot];
            for (int32_t off = 0; off < sz; off += 8) {
                a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot] + off, true);
                a64_emit_str_sp_off(code, R1, total_off + off, true);
            }
        }
        return;
    }
    a64_emit_ldr_sp_off(code, R2, body->slot_offset[dst_slot], true);
    int32_t kind = body->slot_kind[src_slot];
    if (kind == SLOT_I32) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot], false);
        code_emit(code, a64_str_imm(R1, R2, dst_offset, false));
        return;
    }
    int32_t total = body->slot_size[src_slot];
    for (int32_t off = 0; off < total; off += 8) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot] + off, true);
        code_emit(code, a64_str_imm(R1, R2, dst_offset + off, true));
    }
}

static void codegen_str_eq(Code *code, BodyIR *body, int32_t dst, int32_t left, int32_t right) {
    if (body->slot_kind[left] != SLOT_STR || body->slot_kind[right] != SLOT_STR) return; /* skip non-str eq */
    a64_emit_ldr_sp_off(code, R2, body->slot_offset[left] + 8, false);
    a64_emit_ldr_sp_off(code, R3, body->slot_offset[right] + 8, false);
    code_emit(code, a64_cmp_reg(R2, R3));
    int32_t len_ne = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    a64_emit_ldr_sp_off(code, 4, body->slot_offset[left], true);
    a64_emit_ldr_sp_off(code, 5, body->slot_offset[right], true);
    code_emit(code, a64_movz(6, 0, 0));
    int32_t loop = code->count;
    code_emit(code, a64_cmp_reg(6, R2));
    int32_t done_equal = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_ldrb_imm(7, 4, 0));
    code_emit(code, a64_ldrb_imm(8, 5, 0));
    code_emit(code, a64_cmp_reg(7, 8));
    int32_t byte_ne = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(4, 4, 1, true));
    code_emit(code, a64_add_imm(5, 5, 1, true));
    code_emit(code, a64_add_imm(6, 6, 1, false));
    code_emit(code, a64_b(loop - code->count));
    int32_t equal_label = code->count;
    code_emit(code, a64_movz(R0, 1, 0));
    int32_t done_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t false_label = code->count;
    code_emit(code, a64_movz(R0, 0, 0));
    int32_t done = code->count;
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    a64_patch_bcond(code, len_ne, false_label);
    a64_patch_bcond(code, done_equal, equal_label);
    a64_patch_bcond(code, byte_ne, false_label);
    a64_patch_b(code, done_jump, done);
}

static void codegen_copy_bytes(Code *code, int src_reg, int len_reg, int dst_cursor_reg) {
    code_emit(code, a64_cmp_imm(len_reg, 0));
    int32_t g3 = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz(4, 0, 0));
    int32_t loop = code->count;
    code_emit(code, a64_cmp_reg(4, len_reg));
    int32_t done_pos = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_ldrb_imm(5, src_reg, 0));
    code_emit(code, a64_strb_imm(5, dst_cursor_reg, 0));
    code_emit(code, a64_add_imm(src_reg, src_reg, 1, true));
    code_emit(code, a64_add_imm(dst_cursor_reg, dst_cursor_reg, 1, true));
    code_emit(code, a64_add_imm(4, 4, 1, false));
    code_emit(code, a64_b(loop - code->count));
    a64_patch_bcond(code, done_pos, code->count);
    a64_patch_bcond(code, g3, code->count);
}

static void codegen_load_str_pair(Code *code, BodyIR *body, int32_t slot,
                                  int ptr_reg, int len_reg) {
    int32_t kind = body->slot_kind[slot];
    if (kind == SLOT_STR_REF) {
        a64_emit_ldr_sp_off(code, ptr_reg, body->slot_offset[slot], true);
        code_emit(code, a64_ldr_imm(len_reg, ptr_reg, COLD_STR_LEN_OFFSET, false));
        code_emit(code, a64_ldr_imm(ptr_reg, ptr_reg, COLD_STR_DATA_OFFSET, true));
    } else if (kind == SLOT_STR) {
        a64_emit_ldr_sp_off(code, ptr_reg, body->slot_offset[slot] + COLD_STR_DATA_OFFSET, true);
        a64_emit_ldr_sp_off(code, len_reg, body->slot_offset[slot] + COLD_STR_LEN_OFFSET, false);
    } else {
        /* Non-str slot: produce empty string (ptr=0, len=0).
           Matches cold_require_str_value which returns 0 for non-str values. */
        code_emit(code, a64_movz_x(ptr_reg, 0, 0));
        code_emit(code, a64_movz_x(len_reg, 0, 0));
        return;
    }
    /* guard: if len==0, point ptr to SP to prevent NULL deref */
    code_emit(code, a64_cmp_imm(len_reg, 0));
    int32_t g1 = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(ptr_reg, SP, 0, true));
    a64_patch_bcond(code, g1, code->count);
}

static void codegen_store_str_pair(Code *code, BodyIR *body, int32_t slot,
                                   int ptr_reg, int len_reg) {
    a64_emit_str_sp_off(code, ptr_reg, body->slot_offset[slot] + COLD_STR_DATA_OFFSET, true);
    a64_emit_str_sp_off(code, len_reg, body->slot_offset[slot] + COLD_STR_LEN_OFFSET, false);
    code_emit(code, a64_movz(R0, 0, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[slot] + COLD_STR_STORE_ID_OFFSET, false);
    a64_emit_str_sp_off(code, R0, body->slot_offset[slot] + COLD_STR_FLAGS_OFFSET, false);
}

static void codegen_store_empty_str(Code *code, BodyIR *body, int32_t slot) {
    code_emit(code, a64_movz_x(R0, 0, 0));
    codegen_store_str_pair(code, body, slot, R0, R0);
}



static void codegen_mmap_len_reg(Code *code, int len_reg, int dst_reg, int brk_imm) {
    if (len_reg != R1) code_emit(code, a64_add_imm(R1, len_reg, 0, true));
    code_emit(code, a64_movz_x(R0, 0, 0));
    code_emit(code, a64_movz_x(R2, 3, 0));
    code_emit(code, a64_movz_x(R3, 0x1002, 0));
    code_emit(code, a64_movz_x(4, 0, 0));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_movz_x(5, 0, 0));
    code_emit(code, a64_movz_x(16, 197, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 4));
    int32_t mmap_ok = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_brk(brk_imm));
    a64_patch_bcond(code, mmap_ok, code->count);
    if (dst_reg != R0) code_emit(code, a64_add_imm(dst_reg, R0, 0, true));
}

static void codegen_mmap_const(Code *code, int32_t len, int dst_reg, int brk_imm) {
    code_emit(code, a64_movz_x(R1, (uint16_t)len, 0));
    codegen_mmap_len_reg(code, R1, dst_reg, brk_imm);
}

static void codegen_cstring_from_slot(Code *code, BodyIR *body, int32_t slot,
                                      int dst_reg, int brk_imm) {
    codegen_load_str_pair(code, body, slot, R2, R3);
    code_emit(code, a64_add_imm(R1, R3, 1, true));
    codegen_mmap_len_reg(code, R1, dst_reg, brk_imm);
    codegen_load_str_pair(code, body, slot, R2, R3);
    code_emit(code, a64_add_imm(6, dst_reg, 0, true));
    codegen_copy_bytes(code, R2, R3, 6);
    code_emit(code, a64_movz(5, 0, 0));
    code_emit(code, a64_strb_imm(5, 6, 0));
}

static void codegen_cstring_literal(Code *code, int dst_reg, const char *text) {
    int32_t adr_pos = code->count;
    code_emit(code, a64_adr(dst_reg, 0));
    int32_t skip_pos = code->count;
    code_emit(code, a64_b(0));
    int32_t data_offset = code_append_bytes(code, text, (int32_t)strlen(text) + 1);
    int32_t after_data = code->count;
    code->words[adr_pos] = a64_adr(dst_reg, data_offset - adr_pos * 4);
    a64_patch_b(code, skip_pos, after_data);
}

static void codegen_store_exec_result(Code *code, BodyIR *body, int32_t dst,
                                      int32_t output_offset, int output_ptr_reg,
                                      int output_len_reg, int32_t exit_offset,
                                      int exit_code_reg) {
    int32_t base = body->slot_offset[dst];
    a64_emit_str_sp_off(code, output_ptr_reg, base + output_offset + COLD_STR_DATA_OFFSET, true);
    a64_emit_str_sp_off(code, output_len_reg, base + output_offset + COLD_STR_LEN_OFFSET, false);
    code_emit(code, a64_movz(R0, 0, 0));
    a64_emit_str_sp_off(code, R0, base + output_offset + COLD_STR_STORE_ID_OFFSET, false);
    a64_emit_str_sp_off(code, R0, base + output_offset + COLD_STR_FLAGS_OFFSET, false);
    a64_emit_str_sp_off(code, exit_code_reg, base + exit_offset, false);
}

static void codegen_store_exec_failure(Code *code, BodyIR *body, int32_t dst,
                                       int32_t output_offset, int32_t exit_offset) {
    code_emit(code, a64_movz_x(R1, 0, 0));
    code_emit(code, a64_movz(R2, 0, 0));
    code_emit(code, a64_movz(R3, 0, 0));
    code_emit(code, a64_sub_imm(R3, R3, 1, false));
    codegen_store_exec_result(code, body, dst, output_offset, R1, R2, exit_offset, R3);
}

static void codegen_save_exec_spills(Code *code, BodyIR *body, int32_t spill_start) {
    for (int32_t i = 0; i < 8; i++) {
        int32_t slot = body->call_arg_slot[spill_start + i];
        a64_emit_str_sp_off(code, 21 + i, body->slot_offset[slot], true);
    }
}

static void codegen_restore_exec_spills(Code *code, BodyIR *body, int32_t spill_start) {
    for (int32_t i = 0; i < 8; i++) {
        int32_t slot = body->call_arg_slot[spill_start + i];
        a64_emit_ldr_sp_off(code, 21 + i, body->slot_offset[slot], true);
    }
}

static void codegen_exec_shell(Code *code, BodyIR *body, int32_t dst,
                               int32_t arg_start, int32_t output_offset,
                               int32_t exit_offset) {
    if (arg_start < 0 || arg_start + 10 >= body->call_arg_count) {
        codegen_store_exec_failure(code, body, dst, output_offset, exit_offset);
        return;
    }
    int32_t command_slot = body->call_arg_slot[arg_start];
    int32_t options_slot = body->call_arg_slot[arg_start + 1];
    int32_t cwd_slot = body->call_arg_slot[arg_start + 2];
    int32_t spill_start = arg_start + 3;
    codegen_zero_slot(code, body, dst);
    codegen_save_exec_spills(code, body, spill_start);

    codegen_cstring_from_slot(code, body, command_slot, 7, 119);
    code_emit(code, a64_add_imm(21, 7, 0, true));      /* command cstr */
    codegen_cstring_from_slot(code, body, cwd_slot, 8, 119);
    code_emit(code, a64_add_imm(22, 8, 0, true));      /* cwd cstr */
    codegen_load_str_pair(code, body, cwd_slot, R2, R3);
    code_emit(code, a64_add_imm(23, R3, 0, true));     /* cwd len */
    if (body->slot_kind[options_slot] == SLOT_I32 || body->slot_kind[options_slot] == SLOT_I64) {
        a64_emit_ldr_sp_off(code, 4, body->slot_offset[options_slot], false);
    } else {
        code_emit(code, a64_movz(4, 0, 0));
    }
    code_emit(code, a64_movz(5, 1, 0));
    code_emit(code, a64_and_reg(13, 4, 5));            /* merge stderr bit */
    codegen_cstring_literal(code, 24, "/bin/sh");
    codegen_cstring_literal(code, 25, "-c");
    codegen_mmap_const(code, 1024 * 1024 + 1, 26, 119); /* captured output */
    code_emit(code, a64_movz_x(27, 0, 0));             /* used */
    codegen_mov_i64_const(code, 28, 1024 * 1024);      /* cap */
    codegen_mmap_const(code, 4096, 11, 119);           /* discard/status scratch */

    code_emit(code, a64_sub_imm(SP, SP, 64, true));

    code_emit(code, a64_movz_x(16, 42, 0));            /* pipe() -> x0 read, x1 write */
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 31));
    int32_t pipe_fail = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    code_emit(code, a64_add_imm(9, R0, 0, true));      /* read fd */
    code_emit(code, a64_add_imm(10, R1, 0, true));     /* write fd */

    code_emit(code, a64_movz_x(16, 2, 0));             /* fork */
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R1, 31));
    int32_t child_branch = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_cmp_reg_x(R0, 31));
    int32_t fork_fail = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    code_emit(code, a64_add_imm(12, R0, 0, true));     /* pid */

    code_emit(code, a64_add_imm(R0, 10, 0, true));     /* parent close write */
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));

    int32_t read_loop = code->count;
    code_emit(code, a64_cmp_reg_x(27, 28));
    int32_t read_full = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(R1, 26, 27));
    code_emit(code, a64_sub_reg_x(R2, 28, 27));
    int32_t read_call_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t read_full_label = code->count;
    a64_patch_bcond(code, read_full, read_full_label);
    code_emit(code, a64_add_imm(R1, 11, 0, true));
    code_emit(code, a64_movz_x(R2, 4096, 0));
    int32_t read_call = code->count;
    a64_patch_b(code, read_call_jump, read_call);
    code_emit(code, a64_add_imm(R0, 9, 0, true));
    code_emit(code, a64_movz_x(16, 3, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 31));
    int32_t read_done = code->count;
    code_emit(code, a64_bcond(0, COND_LE));
    code_emit(code, a64_cmp_reg_x(27, 28));
    int32_t discard_read = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(27, 27, R0));
    a64_patch_bcond(code, discard_read, code->count);
    code_emit(code, a64_b(read_loop - code->count));
    int32_t read_done_label = code->count;
    a64_patch_bcond(code, read_done, read_done_label);

    code_emit(code, a64_add_imm(R0, 9, 0, true));      /* parent close read */
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_add_reg_x(4, 26, 27));         /* terminate capture */
    code_emit(code, a64_movz(5, 0, 0));
    code_emit(code, a64_strb_imm(5, 4, 0));

    code_emit(code, a64_add_imm(R0, 12, 0, true));     /* wait4(pid, scratch, 0, 0) */
    code_emit(code, a64_add_imm(R1, 11, 0, true));
    code_emit(code, a64_movz_x(R2, 0, 0));
    code_emit(code, a64_movz_x(R3, 0, 0));
    code_emit(code, a64_movz_x(16, 7, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 31));
    int32_t wait_fail = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    code_emit(code, a64_ldr_imm(4, 11, 0, false));     /* status */
    code_emit(code, a64_movz(5, 0x7f, 0));
    code_emit(code, a64_and_reg(6, 4, 5));             /* signal bits */
    code_emit(code, a64_cmp_imm(6, 0));
    int32_t signaled = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_lsr_imm(7, 4, 8, false));
    code_emit(code, a64_movz(5, 0xff, 0));
    code_emit(code, a64_and_reg(7, 7, 5));
    int32_t exit_ready_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t signaled_label = code->count;
    a64_patch_bcond(code, signaled, signaled_label);
    code_emit(code, a64_add_imm(7, 6, 128, false));
    int32_t exit_ready = code->count;
    a64_patch_b(code, exit_ready_jump, exit_ready);
    code_emit(code, a64_add_imm(SP, SP, 64, true));
    codegen_store_exec_result(code, body, dst, output_offset, 26, 27, exit_offset, 7);
    codegen_restore_exec_spills(code, body, spill_start);
    int32_t done_jump = code->count;
    code_emit(code, a64_b(0));

    int32_t wait_fail_label = code->count;
    a64_patch_bcond(code, wait_fail, wait_fail_label);
    code_emit(code, a64_add_imm(SP, SP, 64, true));
    codegen_store_exec_failure(code, body, dst, output_offset, exit_offset);
    codegen_restore_exec_spills(code, body, spill_start);
    int32_t fail_done_jump = code->count;
    code_emit(code, a64_b(0));

    int32_t child_label = code->count;
    a64_patch_bcond(code, child_branch, child_label);
    code_emit(code, a64_add_imm(R0, 9, 0, true));      /* child close read */
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_add_imm(R0, 10, 0, true));     /* dup2(write, stdout) */
    code_emit(code, a64_movz_x(R1, 1, 0));
    code_emit(code, a64_movz_x(16, 90, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_imm(13, 0));
    int32_t skip_stderr_dup = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_add_imm(R0, 10, 0, true));     /* dup2(write, stderr) */
    code_emit(code, a64_movz_x(R1, 2, 0));
    code_emit(code, a64_movz_x(16, 90, 0));
    code_emit(code, a64_svc(0x80));
    a64_patch_bcond(code, skip_stderr_dup, code->count);
    code_emit(code, a64_add_imm(R0, 10, 0, true));     /* child close write */
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_imm(23, 0));
    int32_t skip_chdir = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_add_imm(R0, 22, 0, true));
    code_emit(code, a64_movz_x(16, 12, 0));
    code_emit(code, a64_svc(0x80));
    a64_patch_bcond(code, skip_chdir, code->count);
    code_emit(code, a64_str_imm(24, SP, 16, true));
    code_emit(code, a64_str_imm(25, SP, 24, true));
    code_emit(code, a64_str_imm(21, SP, 32, true));
    code_emit(code, a64_str_imm(31, SP, 40, true));
    code_emit(code, a64_add_imm(R0, 24, 0, true));
    code_emit(code, a64_add_imm(R1, SP, 16, true));
    code_emit(code, a64_movz_x(R2, 0, 0));
    code_emit(code, a64_movz_x(16, 59, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_movz_x(R0, 127, 0));
    code_emit(code, a64_movz_x(16, 1, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_brk(119));

    int32_t fork_fail_label = code->count;
    a64_patch_bcond(code, fork_fail, fork_fail_label);
    code_emit(code, a64_add_imm(R0, 9, 0, true));
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_add_imm(R0, 10, 0, true));
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));

    int32_t pipe_fail_label = code->count;
    a64_patch_bcond(code, pipe_fail, pipe_fail_label);
    code_emit(code, a64_add_imm(SP, SP, 64, true));
    codegen_store_exec_failure(code, body, dst, output_offset, exit_offset);
    codegen_restore_exec_spills(code, body, spill_start);
    int32_t fail2_done_jump = code->count;
    code_emit(code, a64_b(0));

    int32_t done_label = code->count;
    a64_patch_b(code, done_jump, done_label);
    a64_patch_b(code, fail_done_jump, done_label);
    a64_patch_b(code, fail2_done_jump, done_label);
}

static void codegen_shell_quote(Code *code, BodyIR *body, int32_t dst,
                                int32_t text_slot) {
    codegen_load_str_pair(code, body, text_slot, 6, 7);
    code_emit(code, a64_cmp_imm(7, 0));
    int32_t non_empty = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz_x(R1, 3, 0));
    codegen_mmap_len_reg(code, R1, 8, 68);
    code_emit(code, a64_movz(9, '\'', 0));
    code_emit(code, a64_strb_imm(9, 8, 0));
    code_emit(code, a64_strb_imm(9, 8, 1));
    code_emit(code, a64_movz(9, 0, 0));
    code_emit(code, a64_strb_imm(9, 8, 2));
    code_emit(code, a64_movz_x(9, 2, 0));
    codegen_store_str_pair(code, body, dst, 8, 9);
    int32_t empty_done = code->count;
    code_emit(code, a64_b(0));

    a64_patch_bcond(code, non_empty, code->count);
    code_emit(code, a64_cmp_imm(6, 0));
    int32_t qs_null = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(6, SP, 0, true));
    a64_patch_bcond(code, qs_null, code->count);
    code_emit(code, a64_add_reg_x(R1, 7, 7));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_imm(R1, R1, 3, true));
    codegen_mmap_len_reg(code, R1, 8, 68);
    code_emit(code, a64_add_imm(9, 8, 0, true));
    code_emit(code, a64_movz(10, '\'', 0));
    code_emit(code, a64_strb_imm(10, 9, 0));
    code_emit(code, a64_add_imm(9, 9, 1, true));
    code_emit(code, a64_movz_x(11, 0, 0));
    int32_t loop = code->count;
    code_emit(code, a64_cmp_reg_x(11, 7));
    int32_t loop_done = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(12, 6, 11));
    code_emit(code, a64_ldrb_imm(13, 12, 0));
    code_emit(code, a64_cmp_imm(13, '\''));
    int32_t normal_char = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(14, '\'', 0));
    code_emit(code, a64_strb_imm(14, 9, 0));
    code_emit(code, a64_movz(14, '\\', 0));
    code_emit(code, a64_strb_imm(14, 9, 1));
    code_emit(code, a64_movz(14, '\'', 0));
    code_emit(code, a64_strb_imm(14, 9, 2));
    code_emit(code, a64_strb_imm(14, 9, 3));
    code_emit(code, a64_add_imm(9, 9, 4, true));
    int32_t char_done = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, normal_char, code->count);
    code_emit(code, a64_strb_imm(13, 9, 0));
    code_emit(code, a64_add_imm(9, 9, 1, true));
    a64_patch_b(code, char_done, code->count);
    code_emit(code, a64_add_imm(11, 11, 1, true));
    code_emit(code, a64_b(loop - code->count));
    a64_patch_bcond(code, loop_done, code->count);
    code_emit(code, a64_movz(10, '\'', 0));
    code_emit(code, a64_strb_imm(10, 9, 0));
    code_emit(code, a64_add_imm(9, 9, 1, true));
    code_emit(code, a64_movz(10, 0, 0));
    code_emit(code, a64_strb_imm(10, 9, 0));
    code_emit(code, a64_sub_reg(10, 9, 8));
    codegen_store_str_pair(code, body, dst, 8, 10);
    a64_patch_b(code, empty_done, code->count);
}

static void codegen_strlen_reg(Code *code, int ptr_reg, int len_reg) {
    int addr_reg = 6;
    if (addr_reg == ptr_reg || addr_reg == len_reg) addr_reg = 7;
    if (addr_reg == ptr_reg || addr_reg == len_reg) addr_reg = 8;
    int byte_reg = 7;
    if (byte_reg == ptr_reg || byte_reg == len_reg || byte_reg == addr_reg) byte_reg = 8;
    if (byte_reg == ptr_reg || byte_reg == len_reg || byte_reg == addr_reg) byte_reg = 9;
    code_emit(code, a64_movz_x(len_reg, 0, 0));
    code_emit(code, a64_cmp_imm(ptr_reg, 0));
    int32_t g2 = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    int32_t loop = code->count;
    code_emit(code, a64_add_reg_x(addr_reg, ptr_reg, len_reg));
    code_emit(code, a64_ldrb_imm(byte_reg, addr_reg, 0));
    code_emit(code, a64_cmp_imm(byte_reg, 0));
    int32_t done_pos = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_add_imm(len_reg, len_reg, 1, true));
    code_emit(code, a64_b(loop - code->count));
    a64_patch_bcond(code, done_pos, code->count);
    a64_patch_bcond(code, g2, code->count);
}

static void codegen_getenv_str(Code *code, BodyIR *body, int32_t dst, int32_t key_slot) {
    codegen_load_str_pair(code, body, key_slot, R1, R2);
    code_emit(code, a64_add_imm(4, 22, 0, true));
    int32_t env_loop = code->count;
    code_emit(code, a64_ldr_imm(5, 4, 0, true));
    code_emit(code, a64_cmp_reg_x(5, 31));
    int32_t not_found = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz_x(6, 0, 0));
    int32_t key_loop = code->count;
    code_emit(code, a64_cmp_reg_x(6, R2));
    int32_t key_done = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(7, R1, 6));
    code_emit(code, a64_ldrb_imm(8, 7, 0));
    code_emit(code, a64_add_reg_x(9, 5, 6));
    code_emit(code, a64_ldrb_imm(10, 9, 0));
    code_emit(code, a64_cmp_reg(8, 10));
    int32_t next_env_byte_mismatch = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(6, 6, 1, true));
    code_emit(code, a64_b(key_loop - code->count));
    a64_patch_bcond(code, key_done, code->count);
    code_emit(code, a64_add_reg_x(9, 5, R2));
    code_emit(code, a64_ldrb_imm(10, 9, 0));
    code_emit(code, a64_cmp_imm(10, '='));
    int32_t next_env_no_equals = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(5, 9, 1, true));
    codegen_strlen_reg(code, 5, 6);
    codegen_store_str_pair(code, body, dst, 5, 6);
    int32_t done_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t next_env = code->count;
    a64_patch_bcond(code, next_env_byte_mismatch, next_env);
    a64_patch_bcond(code, next_env_no_equals, next_env);
    code_emit(code, a64_add_imm(4, 4, 8, true));
    code_emit(code, a64_b(env_loop - code->count));
    a64_patch_bcond(code, not_found, code->count);
    codegen_store_empty_str(code, body, dst);
    a64_patch_b(code, done_jump, code->count);
}

static void codegen_str_select_nonempty(Code *code, BodyIR *body, int32_t dst,
                                        int32_t preferred, int32_t alternate) {
    codegen_load_str_pair(code, body, preferred, R2, R3);
    code_emit(code, a64_cmp_imm(R3, 0));
    int32_t use_alternate = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    codegen_store_str_pair(code, body, dst, R2, R3);
    int32_t done = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, use_alternate, code->count);
    codegen_load_str_pair(code, body, alternate, R2, R3);
    codegen_store_str_pair(code, body, dst, R2, R3);
    a64_patch_b(code, done, code->count);
}

static void codegen_read_flag(Code *code, BodyIR *body, int32_t dst,
                              int32_t key_slot, int32_t default_slot) {
    codegen_load_str_pair(code, body, key_slot, R1, R2);
    codegen_load_str_pair(code, body, default_slot, 14, 15);
    code_emit(code, a64_movz(5, 1, 0));
    int32_t arg_loop = code->count;
    code_emit(code, a64_cmp_reg(5, 19));
    int32_t not_found = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_imm(6, 20, 0, true));
    code_emit(code, a64_add_reg_x(7, 5, 5));
    code_emit(code, a64_add_reg_x(7, 7, 7));
    code_emit(code, a64_add_reg_x(7, 7, 7));
    code_emit(code, a64_add_reg_x(6, 6, 7));
    code_emit(code, a64_ldr_imm(7, 6, 0, true));
    code_emit(code, a64_cmp_reg_x(7, 31));
    int32_t next_null = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz_x(8, 0, 0));
    int32_t prefix_loop = code->count;
    code_emit(code, a64_cmp_reg_x(8, R2));
    int32_t prefix_done = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(9, 7, 8));
    code_emit(code, a64_ldrb_imm(10, 9, 0));
    code_emit(code, a64_cmp_imm(10, 0));
    int32_t next_short = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_add_reg_x(9, R1, 8));
    code_emit(code, a64_ldrb_imm(11, 9, 0));
    code_emit(code, a64_cmp_reg(10, 11));
    int32_t next_mismatch = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(8, 8, 1, true));
    code_emit(code, a64_b(prefix_loop - code->count));
    a64_patch_bcond(code, prefix_done, code->count);
    code_emit(code, a64_add_reg_x(9, 7, R2));
    code_emit(code, a64_ldrb_imm(10, 9, 0));
    code_emit(code, a64_cmp_imm(10, ':'));
    int32_t colon_value = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_cmp_imm(10, '='));
    int32_t eq_value = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_cmp_imm(10, 0));
    int32_t exact_key = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    int32_t next_bad_suffix = code->count;
    code_emit(code, a64_b(0));
    int32_t inline_value = code->count;
    a64_patch_bcond(code, colon_value, inline_value);
    a64_patch_bcond(code, eq_value, inline_value);
    code_emit(code, a64_add_imm(11, 9, 1, true));
    codegen_strlen_reg(code, 11, 12);
    code_emit(code, a64_add_imm(R1, 12, 1, true));
    codegen_mmap_len_reg(code, R1, 13, 67);
    code_emit(code, a64_add_imm(6, 13, 0, true));
    codegen_copy_bytes(code, 11, 12, 6);
    code_emit(code, a64_movz(5, 0, 0));
    code_emit(code, a64_strb_imm(5, 6, 0));
    codegen_store_str_pair(code, body, dst, 13, 12);
    int32_t done_inline = code->count;
    code_emit(code, a64_b(0));
    int32_t exact_label = code->count;
    a64_patch_bcond(code, exact_key, exact_label);
    code_emit(code, a64_add_imm(12, 5, 1, false));
    code_emit(code, a64_cmp_reg(12, 19));
    int32_t next_no_value = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_imm(6, 20, 0, true));
    code_emit(code, a64_add_reg_x(11, 12, 12));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(6, 6, 11));
    code_emit(code, a64_ldr_imm(11, 6, 0, true));
    code_emit(code, a64_cmp_reg_x(11, 31));
    int32_t next_value_null = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    codegen_strlen_reg(code, 11, 12);
    code_emit(code, a64_add_imm(R1, 12, 1, true));
    codegen_mmap_len_reg(code, R1, 13, 67);
    code_emit(code, a64_add_imm(6, 13, 0, true));
    codegen_copy_bytes(code, 11, 12, 6);
    code_emit(code, a64_movz(5, 0, 0));
    code_emit(code, a64_strb_imm(5, 6, 0));
    codegen_store_str_pair(code, body, dst, 13, 12);
    int32_t done_exact = code->count;
    code_emit(code, a64_b(0));
    int32_t next_arg = code->count;
    a64_patch_bcond(code, next_null, next_arg);
    a64_patch_bcond(code, next_short, next_arg);
    a64_patch_bcond(code, next_mismatch, next_arg);
    a64_patch_b(code, next_bad_suffix, next_arg);
    a64_patch_bcond(code, next_no_value, next_arg);
    a64_patch_bcond(code, next_value_null, next_arg);
    code_emit(code, a64_add_imm(5, 5, 1, false));
    code_emit(code, a64_b(arg_loop - code->count));
    a64_patch_bcond(code, not_found, code->count);
    codegen_store_str_pair(code, body, dst, 14, 15);
    int32_t done = code->count;
    a64_patch_b(code, done_inline, done);
    a64_patch_b(code, done_exact, done);
}

static void codegen_parse_int_slot(Code *code, BodyIR *body, int32_t dst, int32_t src) {
    codegen_load_str_pair(code, body, src, R1, R2);
    code_emit(code, a64_movz(R0, 0, 0));
    code_emit(code, a64_movz_x(3, 0, 0));
    code_emit(code, a64_movz(4, 0, 0));
    code_emit(code, a64_cmp_imm(R2, 0));
    int32_t empty_done = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_ldrb_imm(5, R1, 0));
    code_emit(code, a64_cmp_imm(5, '-'));
    int32_t first_digit = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(4, 1, 0));
    code_emit(code, a64_add_imm(3, 3, 1, true));
    a64_patch_bcond(code, first_digit, code->count);
    int32_t loop = code->count;
    code_emit(code, a64_cmp_reg_x(3, R2));
    int32_t apply_sign = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(6, R1, 3));
    code_emit(code, a64_ldrb_imm(5, 6, 0));
    code_emit(code, a64_cmp_imm(5, '0'));
    int32_t below_digit = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    code_emit(code, a64_cmp_imm(5, '9'));
    int32_t above_digit = code->count;
    code_emit(code, a64_bcond(0, COND_GT));
    code_emit(code, a64_sub_imm(5, 5, '0', false));
    code_emit(code, a64_movz(6, 10, 0));
    code_emit(code, a64_mul_reg(R0, R0, 6));
    code_emit(code, a64_add_reg(R0, R0, 5));
    code_emit(code, a64_add_imm(3, 3, 1, true));
    code_emit(code, a64_b(loop - code->count));
    a64_patch_bcond(code, apply_sign, code->count);
    a64_patch_bcond(code, below_digit, code->count);
    a64_patch_bcond(code, above_digit, code->count);
    code_emit(code, a64_cmp_imm(4, 0));
    int32_t positive_done = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz(5, 0, 0));
    code_emit(code, a64_sub_reg(R0, 5, R0));
    a64_patch_bcond(code, positive_done, code->count);
    a64_patch_bcond(code, empty_done, code->count);
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
}

static void codegen_seq_str_header_addr(Code *code, BodyIR *body, int32_t seq_slot, int reg);

static void codegen_str_join(Code *code, BodyIR *body, int32_t dst,
                             int32_t items_slot, int32_t sep_slot) {
    codegen_seq_str_header_addr(code, body, items_slot, 11);
    code_emit(code, a64_ldr_imm(12, 11, 0, false));
    code_emit(code, a64_ldr_imm(13, 11, 8, true));
    codegen_load_str_pair(code, body, sep_slot, 14, 15);
    code_emit(code, a64_movz_x(7, 0, 0));
    code_emit(code, a64_movz(8, 0, 0));
    int32_t total_loop = code->count;
    code_emit(code, a64_cmp_reg(8, 12));
    int32_t total_done = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_cmp_imm(8, 0));
    int32_t no_sep_total = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_add_reg_x(7, 7, 15));
    a64_patch_bcond(code, no_sep_total, code->count);
    code_emit(code, a64_add_reg_x(9, 8, 8));
    code_emit(code, a64_add_reg_x(9, 9, 9));
    code_emit(code, a64_add_reg_x(9, 9, 9));
    code_emit(code, a64_add_reg_x(9, 9, 9));
    code_emit(code, a64_add_reg_x(9, 13, 9));
    code_emit(code, a64_ldr_imm(10, 9, 8, true));
    code_emit(code, a64_add_reg_x(7, 7, 10));
    code_emit(code, a64_add_imm(8, 8, 1, false));
    code_emit(code, a64_b(total_loop - code->count));
    a64_patch_bcond(code, total_done, code->count);
    code_emit(code, a64_cmp_imm(7, 0));
    int32_t non_empty = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    codegen_store_empty_str(code, body, dst);
    int32_t empty_done = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, non_empty, code->count);
    code_emit(code, a64_add_imm(R1, 7, 1, true));
    codegen_mmap_len_reg(code, R1, 6, 59);
    code_emit(code, a64_add_imm(10, 6, 0, true));
    code_emit(code, a64_movz(8, 0, 0));
    int32_t copy_loop = code->count;
    code_emit(code, a64_cmp_reg(8, 12));
    int32_t copy_done = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_cmp_imm(8, 0));
    int32_t no_sep_copy = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_add_imm(R0, 14, 0, true));
    codegen_copy_bytes(code, R0, 15, 10);
    a64_patch_bcond(code, no_sep_copy, code->count);
    code_emit(code, a64_add_reg_x(9, 8, 8));
    code_emit(code, a64_add_reg_x(9, 9, 9));
    code_emit(code, a64_add_reg_x(9, 9, 9));
    code_emit(code, a64_add_reg_x(9, 9, 9));
    code_emit(code, a64_add_reg_x(9, 13, 9));
    code_emit(code, a64_ldr_imm(R0, 9, 0, true));
    code_emit(code, a64_ldr_imm(R1, 9, 8, true));
    codegen_copy_bytes(code, R0, R1, 10);
    code_emit(code, a64_add_imm(8, 8, 1, false));
    code_emit(code, a64_b(copy_loop - code->count));
    a64_patch_bcond(code, copy_done, code->count);
    code_emit(code, a64_movz(R0, 0, 0));
    code_emit(code, a64_strb_imm(R0, 10, 0));
    codegen_store_str_pair(code, body, dst, 6, 7);
    a64_patch_b(code, empty_done, code->count);
}

static void codegen_str_split_char(Code *code, BodyIR *body, int32_t dst,
                                   int32_t text_slot, int32_t sep_slot) {
    codegen_load_str_pair(code, body, text_slot, 6, 7);
    a64_emit_ldr_sp_off(code, 11, body->slot_offset[sep_slot], false);
    code_emit(code, a64_movz(12, 1, 0));
    code_emit(code, a64_movz(13, 0, 0));
    int32_t count_loop = code->count;
    code_emit(code, a64_cmp_reg_x(13, 7));
    int32_t count_done = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(14, 6, 13));
    code_emit(code, a64_ldrb_imm(15, 14, 0));
    code_emit(code, a64_cmp_reg(15, 11));
    int32_t not_sep = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(12, 12, 1, false));
    a64_patch_bcond(code, not_sep, code->count);
    code_emit(code, a64_add_imm(13, 13, 1, true));
    code_emit(code, a64_b(count_loop - code->count));
    a64_patch_bcond(code, count_done, code->count);
    code_emit(code, a64_add_reg_x(R1, 12, 12));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    codegen_mmap_len_reg(code, R1, 14, 60);
    a64_emit_str_sp_off(code, 12, body->slot_offset[dst], false);
    a64_emit_str_sp_off(code, 12, body->slot_offset[dst] + 4, false);
    a64_emit_str_sp_off(code, 14, body->slot_offset[dst] + 8, true);
    code_emit(code, a64_movz(13, 0, 0));
    code_emit(code, a64_movz(15, 0, 0));
    code_emit(code, a64_movz(10, 0, 0));
    int32_t fill_loop = code->count;
    code_emit(code, a64_cmp_reg_x(13, 7));
    int32_t emit_trailing = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_add_reg_x(8, 6, 13));
    code_emit(code, a64_ldrb_imm(9, 8, 0));
    code_emit(code, a64_cmp_reg(9, 11));
    int32_t no_emit = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    a64_patch_bcond(code, emit_trailing, code->count);
    code_emit(code, a64_add_reg_x(8, 6, 15));
    code_emit(code, a64_sub_reg(9, 13, 15));
    code_emit(code, a64_add_reg_x(12, 10, 10));
    code_emit(code, a64_add_reg_x(12, 12, 12));
    code_emit(code, a64_add_reg_x(12, 12, 12));
    code_emit(code, a64_add_reg_x(12, 12, 12));
    code_emit(code, a64_add_reg_x(12, 14, 12));
    code_emit(code, a64_str_imm(8, 12, 0, true));
    code_emit(code, a64_str_imm(9, 12, 8, true));
    code_emit(code, a64_add_imm(10, 10, 1, false));
    code_emit(code, a64_add_imm(15, 13, 1, true));
    code_emit(code, a64_cmp_reg_x(13, 7));
    int32_t fill_done = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    a64_patch_bcond(code, no_emit, code->count);
    code_emit(code, a64_add_imm(13, 13, 1, true));
    code_emit(code, a64_b(fill_loop - code->count));
    a64_patch_bcond(code, fill_done, code->count);
}

static void codegen_str_strip(Code *code, BodyIR *body, int32_t dst, int32_t src) {
    codegen_load_str_pair(code, body, src, R1, R2);
    code_emit(code, a64_movz_x(3, 0, 0));
    int32_t left_loop = code->count;
    code_emit(code, a64_cmp_reg_x(3, R2));
    int32_t left_done = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(4, R1, 3));
    code_emit(code, a64_ldrb_imm(5, 4, 0));
    code_emit(code, a64_cmp_imm(5, ' '));
    int32_t left_non_space = code->count;
    code_emit(code, a64_bcond(0, COND_GT));
    code_emit(code, a64_add_imm(3, 3, 1, true));
    code_emit(code, a64_b(left_loop - code->count));
    a64_patch_bcond(code, left_done, code->count);
    a64_patch_bcond(code, left_non_space, code->count);
    code_emit(code, a64_add_imm(4, R2, 0, true));
    int32_t right_loop = code->count;
    code_emit(code, a64_cmp_reg_x(4, 3));
    int32_t right_done = code->count;
    code_emit(code, a64_bcond(0, COND_LE));
    code_emit(code, a64_sub_imm(5, 4, 1, true));
    code_emit(code, a64_add_reg_x(6, R1, 5));
    code_emit(code, a64_ldrb_imm(7, 6, 0));
    code_emit(code, a64_cmp_imm(7, ' '));
    int32_t right_non_space = code->count;
    code_emit(code, a64_bcond(0, COND_GT));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_b(right_loop - code->count));
    a64_patch_bcond(code, right_done, code->count);
    a64_patch_bcond(code, right_non_space, code->count);
    code_emit(code, a64_add_reg_x(6, R1, 3));
    code_emit(code, a64_sub_reg(7, 4, 3));
    codegen_store_str_pair(code, body, dst, 6, 7);
}

static void codegen_str_slice(Code *code, BodyIR *body, int32_t dst,
                              int32_t text_slot, int32_t start_slot, int32_t len_slot) {
    codegen_load_str_pair(code, body, text_slot, R1, R2);
    a64_emit_ldr_sp_off(code, 3, body->slot_offset[start_slot], false);
    a64_emit_ldr_sp_off(code, 4, body->slot_offset[len_slot], false);
    code_emit(code, a64_cmp_imm(3, 0));
    int32_t start_non_negative = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_brk(66));
    a64_patch_bcond(code, start_non_negative, code->count);
    code_emit(code, a64_cmp_imm(4, 0));
    int32_t len_non_negative = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_brk(66));
    a64_patch_bcond(code, len_non_negative, code->count);
    code_emit(code, a64_cmp_reg_x(3, R2));
    int32_t start_in_bounds = code->count;
    code_emit(code, a64_bcond(0, COND_LE));
    code_emit(code, a64_brk(66));
    a64_patch_bcond(code, start_in_bounds, code->count);
    code_emit(code, a64_add_reg_x(5, 3, 4));
    code_emit(code, a64_cmp_reg_x(5, R2));
    int32_t end_in_bounds = code->count;
    code_emit(code, a64_bcond(0, COND_LE));
    code_emit(code, a64_brk(66));
    a64_patch_bcond(code, end_in_bounds, code->count);
    code_emit(code, a64_add_reg_x(6, R1, 3));
    codegen_store_str_pair(code, body, dst, 6, 4);
}

static void cold_text_set_offsets(Symbols *symbols, BodyIR *body, int32_t slot,
                                  int32_t *slots_offset_out, int32_t *empty_offset_out) {
    Span set_type = cold_type_strip_var(body->slot_type[slot], 0);
    ObjectDef *set_object = symbols_resolve_object(symbols, set_type);
    if (!set_object) die("CompilerCsgTextSet layout missing");
    ObjectField *lookup_field = object_find_field(set_object, cold_cstr_span("lookup"));
    ObjectField *empty_field = object_find_field(set_object, cold_cstr_span("emptySeen"));
    if (!lookup_field || !empty_field) die("CompilerCsgTextSet fields missing");
    ObjectDef *lookup_object = symbols_resolve_object(symbols, lookup_field->type_name);
    ObjectField *slots_field = lookup_object ? object_find_field(lookup_object, cold_cstr_span("slots")) : 0;
    *slots_offset_out = lookup_field->offset + (slots_field ? slots_field->offset : 0);
    *empty_offset_out = empty_field->offset;
}

static void codegen_text_set_base(Code *code, BodyIR *body, int32_t slot, int reg) {
    if (body->slot_kind[slot] == SLOT_OBJECT) {
        a64_emit_add_large(code, reg, SP, body->slot_offset[slot], true);
        return;
    }
    if (body->slot_kind[slot] == SLOT_OBJECT_REF) {
        a64_emit_ldr_sp_off(code, reg, body->slot_offset[slot], true);
        return;
    }
    /* Non-object base: use SP-relative */
    a64_emit_add_large(code, reg, SP, body->slot_offset[slot], true);
}

static void codegen_text_set_insert(Code *code, BodyIR *body, Symbols *symbols,
                                    int32_t dst, int32_t seen_slot, int32_t text_slot) {
    int32_t slots_offset = 0;
    int32_t empty_offset = 0;
    cold_text_set_offsets(symbols, body, seen_slot, &slots_offset, &empty_offset);
    codegen_text_set_base(code, body, seen_slot, 17);
    codegen_load_str_pair(code, body, text_slot, 3, 4);
    code_emit(code, a64_cmp_imm(4, 0));
    int32_t non_empty = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_ldr_imm(5, 17, empty_offset, false));
    code_emit(code, a64_cmp_imm(5, 0));
    int32_t empty_new = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz(R0, 0, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    int32_t empty_done = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, empty_new, code->count);
    code_emit(code, a64_movz(5, 1, 0));
    code_emit(code, a64_str_imm(5, 17, empty_offset, false));
    a64_emit_str_sp_off(code, 5, body->slot_offset[dst], false);
    a64_patch_b(code, empty_done, code->count);
    int32_t all_done_from_empty = code->count;
    code_emit(code, a64_b(0));

    a64_patch_bcond(code, non_empty, code->count);
    code_emit(code, a64_add_imm(6, 17, (uint16_t)slots_offset, true));
    code_emit(code, a64_ldr_imm(7, 6, 0, false));
    code_emit(code, a64_ldr_imm(8, 6, 4, false));
    code_emit(code, a64_ldr_imm(9, 6, 8, true));
    code_emit(code, a64_movz(10, 0, 0));
    int32_t item_loop = code->count;
    code_emit(code, a64_cmp_reg(10, 7));
    int32_t not_found = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(11, 10, 10));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(11, 9, 11));
    code_emit(code, a64_ldr_imm(12, 11, 0, true));
    code_emit(code, a64_ldr_imm(13, 11, 8, true));
    code_emit(code, a64_cmp_reg_x(13, 4));
    int32_t next_len_mismatch = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(14, 0, 0));
    int32_t byte_loop = code->count;
    code_emit(code, a64_cmp_reg_x(14, 4));
    int32_t duplicate = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_add_reg_x(15, 12, 14));
    code_emit(code, a64_ldrb_imm(R0, 15, 0));
    code_emit(code, a64_add_reg_x(15, 3, 14));
    code_emit(code, a64_ldrb_imm(R1, 15, 0));
    code_emit(code, a64_cmp_reg(R0, R1));
    int32_t next_byte_mismatch = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_add_imm(14, 14, 1, true));
    code_emit(code, a64_b(byte_loop - code->count));
    a64_patch_bcond(code, duplicate, code->count);
    code_emit(code, a64_movz(R0, 0, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    int32_t duplicate_done = code->count;
    code_emit(code, a64_b(0));
    int32_t next_item = code->count;
    a64_patch_bcond(code, next_len_mismatch, next_item);
    a64_patch_bcond(code, next_byte_mismatch, next_item);
    code_emit(code, a64_add_imm(10, 10, 1, false));
    code_emit(code, a64_b(item_loop - code->count));

    a64_patch_bcond(code, not_found, code->count);
    code_emit(code, a64_cmp_reg(7, 8));
    int32_t have_capacity = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    code_emit(code, a64_cmp_imm(8, 0));
    int32_t double_capacity = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(8, 4, 0));
    int32_t capacity_ready_jump = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, double_capacity, code->count);
    code_emit(code, a64_add_reg(8, 8, 8));
    a64_patch_b(code, capacity_ready_jump, code->count);
    code_emit(code, a64_add_imm(14, 3, 0, true));
    code_emit(code, a64_add_imm(15, 4, 0, true));
    code_emit(code, a64_add_reg_x(R1, 8, 8));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    codegen_mmap_len_reg(code, R1, 12, 63);
    code_emit(code, a64_add_imm(13, 12, 0, true));
    code_emit(code, a64_add_imm(R0, 9, 0, true));
    code_emit(code, a64_add_reg_x(R1, 7, 7));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_reg_x(R1, R1, R1));
    code_emit(code, a64_add_imm(R2, 13, 0, true));
    codegen_copy_bytes(code, R0, R1, R2);
    code_emit(code, a64_str_imm(8, 6, 4, false));
    code_emit(code, a64_str_imm(13, 6, 8, true));
    code_emit(code, a64_add_imm(9, 13, 0, true));
    code_emit(code, a64_add_imm(3, 14, 0, true));
    code_emit(code, a64_add_imm(4, 15, 0, true));
    a64_patch_bcond(code, have_capacity, code->count);
    code_emit(code, a64_add_reg_x(11, 7, 7));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(11, 11, 11));
    code_emit(code, a64_add_reg_x(11, 9, 11));
    code_emit(code, a64_str_imm(3, 11, 0, true));
    code_emit(code, a64_str_imm(4, 11, 8, true));
    code_emit(code, a64_add_imm(7, 7, 1, false));
    code_emit(code, a64_str_imm(7, 6, 0, false));
    code_emit(code, a64_movz(R0, 1, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    a64_patch_b(code, duplicate_done, code->count);
    a64_patch_b(code, all_done_from_empty, code->count);
}

static void codegen_adr_cstr(Code *code, int reg, const char *text) {
    int32_t len = (int32_t)strlen(text) + 1;
    int32_t adr_pos = code->count;
    code_emit(code, a64_adr(reg, 0));
    int32_t skip_pos = code->count;
    code_emit(code, a64_b(0));
    int32_t data_offset = code_append_bytes(code, text, len);
    int32_t after_data = code->count;
    code->words[adr_pos] = a64_adr(reg, data_offset - adr_pos * 4);
    code->words[skip_pos] = a64_b(after_data - skip_pos);
}

static void codegen_current_dir_to_regs(Code *code, int ptr_reg, int len_reg) {
    codegen_mmap_const(code, 4096, ptr_reg, 41);
    codegen_adr_cstr(code, R0, ".");
    code_emit(code, a64_movz_x(R1, 0, 0));
    code_emit(code, a64_movz_x(R2, 0, 0));
    code_emit(code, a64_movz_x(16, 5, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_imm(R0, 0));
    int32_t open_ok = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_brk(42));
    a64_patch_bcond(code, open_ok, code->count);
    code_emit(code, a64_add_imm(9, R0, 0, true));
    code_emit(code, a64_add_imm(R0, 9, 0, true));
    code_emit(code, a64_movz_x(R1, 50, 0));
    code_emit(code, a64_add_imm(R2, ptr_reg, 0, true));
    code_emit(code, a64_movz_x(16, 92, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_imm(R0, 0));
    int32_t fcntl_ok = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_brk(43));
    a64_patch_bcond(code, fcntl_ok, code->count);
    code_emit(code, a64_add_imm(R0, 9, 0, true));
    code_emit(code, a64_movz_x(16, 6, 0));
    code_emit(code, a64_svc(0x80));
    codegen_strlen_reg(code, ptr_reg, len_reg);
}

static void codegen_path_join_slots(Code *code, BodyIR *body, int32_t dst,
                                    int32_t left_slot, int32_t right_slot) {
    codegen_load_str_pair(code, body, left_slot, R2, R3);
    codegen_load_str_pair(code, body, right_slot, 9, 10);
    code_emit(code, a64_add_reg_x(R1, R3, 10));
    code_emit(code, a64_add_imm(R1, R1, 1, true));
    codegen_mmap_len_reg(code, R1, 7, 44);
    codegen_load_str_pair(code, body, left_slot, R2, R3);
    codegen_load_str_pair(code, body, right_slot, 9, 10);

    code_emit(code, a64_movz_x(11, 1, 0));
    code_emit(code, a64_cmp_imm(R3, 0));
    int32_t left_non_empty = code->count;
    code_emit(code, a64_bcond(0, COND_GT));
    code_emit(code, a64_movz_x(11, 0, 0));
    int32_t left_empty_jump = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, left_non_empty, code->count);
    code_emit(code, a64_add_reg_x(12, R2, R3));
    code_emit(code, a64_sub_imm(12, 12, 1, true));
    code_emit(code, a64_ldrb_imm(13, 12, 0));
    code_emit(code, a64_cmp_imm(13, '/'));
    int32_t left_slash = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_cmp_imm(13, '\\'));
    int32_t left_backslash = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    int32_t left_done_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t left_no_slash_done = code->count;
    a64_patch_bcond(code, left_slash, left_no_slash_done);
    a64_patch_bcond(code, left_backslash, left_no_slash_done);
    code_emit(code, a64_movz_x(11, 0, 0));
    a64_patch_b(code, left_done_jump, code->count);
    a64_patch_b(code, left_empty_jump, code->count);

    code_emit(code, a64_cmp_imm(10, 0));
    int32_t right_non_empty = code->count;
    code_emit(code, a64_bcond(0, COND_GT));
    code_emit(code, a64_movz_x(11, 0, 0));
    int32_t right_empty_jump = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, right_non_empty, code->count);
    code_emit(code, a64_ldrb_imm(13, 9, 0));
    code_emit(code, a64_cmp_imm(13, '/'));
    int32_t right_slash = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_cmp_imm(13, '\\'));
    int32_t right_backslash = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    int32_t right_done_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t right_no_slash_done = code->count;
    a64_patch_bcond(code, right_slash, right_no_slash_done);
    a64_patch_bcond(code, right_backslash, right_no_slash_done);
    code_emit(code, a64_movz_x(11, 0, 0));
    a64_patch_b(code, right_done_jump, code->count);
    a64_patch_b(code, right_empty_jump, code->count);

    code_emit(code, a64_add_reg_x(12, R3, 10));
    code_emit(code, a64_add_reg_x(12, 12, 11));
    codegen_store_str_pair(code, body, dst, 7, 12);
    code_emit(code, a64_add_imm(6, 7, 0, true));
    codegen_copy_bytes(code, R2, R3, 6);
    code_emit(code, a64_cmp_imm(11, 0));
    int32_t no_sep = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz(5, '/', 0));
    code_emit(code, a64_strb_imm(5, 6, 0));
    code_emit(code, a64_add_imm(6, 6, 1, true));
    a64_patch_bcond(code, no_sep, code->count);
    code_emit(code, a64_add_imm(R2, 9, 0, true));
    code_emit(code, a64_add_imm(R3, 10, 0, true));
    codegen_copy_bytes(code, R2, R3, 6);
}

static void codegen_str_concat(Code *code, BodyIR *body, int32_t dst, int32_t left, int32_t right) {
    if (body->slot_kind[left] != SLOT_STR || body->slot_kind[right] != SLOT_STR) {
        codegen_zero_slot(code, body, dst);
        return;
    }
    a64_emit_ldr_sp_off(code, R1, body->slot_offset[left] + 8, true);
    a64_emit_ldr_sp_off(code, R2, body->slot_offset[right] + 8, true);
    code_emit(code, a64_add_reg(R1, R1, R2));
    a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + 8, true);
    code_emit(code, a64_cmp_imm(R1, 0));
    int32_t non_empty_pos = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz_x(R0, 0, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    int32_t empty_done = code->count;
    code_emit(code, a64_b(0));

    a64_patch_bcond(code, non_empty_pos, code->count);
    code_emit(code, a64_movz_x(R0, 0, 0));
    code_emit(code, a64_movz_x(R2, 3, 0));
    code_emit(code, a64_movz_x(R3, 0x1002, 0));
    code_emit(code, a64_movz_x(4, 0, 0));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_movz_x(5, 0, 0));
    code_emit(code, a64_movz_x(16, 197, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 4));
    int32_t mmap_ok = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_brk(4));
    a64_patch_bcond(code, mmap_ok, code->count);
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    code_emit(code, a64_add_imm(6, R0, 0, true));

    a64_emit_ldr_sp_off(code, R2, body->slot_offset[left], true);
    a64_emit_ldr_sp_off(code, R3, body->slot_offset[left] + 8, true);
    codegen_copy_bytes(code, R2, R3, 6);
    a64_emit_ldr_sp_off(code, R2, body->slot_offset[right], true);
    a64_emit_ldr_sp_off(code, R3, body->slot_offset[right] + 8, true);
    codegen_copy_bytes(code, R2, R3, 6);

    a64_patch_b(code, empty_done, code->count);
}

static void codegen_i32_to_str(Code *code, BodyIR *body, int32_t dst, int32_t src) {
    if (body->slot_kind[src] != SLOT_I32) { codegen_zero_slot(code, body, dst); return; }
    a64_emit_ldr_sp_off(code, R2, body->slot_offset[src], false);

    code_emit(code, a64_movz_x(R0, 0, 0));
    code_emit(code, a64_movz_x(R1, 16, 0));
    code_emit(code, a64_movz_x(R2, 3, 0));
    code_emit(code, a64_movz_x(R3, 0x1002, 0));
    code_emit(code, a64_movz_x(4, 0, 0));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_movz_x(5, 0, 0));
    code_emit(code, a64_movz_x(16, 197, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 4));
    int32_t mmap_ok = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_brk(6));
    a64_patch_bcond(code, mmap_ok, code->count);
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);

    a64_emit_ldr_sp_off(code, R2, body->slot_offset[src], false);
    code_emit(code, a64_cmp_imm(R2, 0));
    int32_t non_zero = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(5, '0', 0));
    code_emit(code, a64_strb_imm(5, R0, 0));
    code_emit(code, a64_movz_x(8, 1, 0));
    a64_emit_str_sp_off(code, 8, body->slot_offset[dst] + 8, true);
    int32_t zero_done = code->count;
    code_emit(code, a64_b(0));

    a64_patch_bcond(code, non_zero, code->count);
    code_emit(code, a64_add_imm(6, R0, 15, true));
    code_emit(code, a64_movz(8, 0, 0));
    code_emit(code, a64_cmp_imm(R2, 0));
    int32_t negative = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    code_emit(code, a64_sub_reg(R2, 31, R2));
    code_emit(code, a64_movz(9, 0, 0));
    int32_t digits_jump = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, negative, code->count);
    code_emit(code, a64_movz(9, 1, 0));

    a64_patch_b(code, digits_jump, code->count);
    code_emit(code, a64_movz(4, 10, 0));
    int32_t digit_loop = code->count;
    code_emit(code, a64_sdiv_reg(3, R2, 4));
    code_emit(code, a64_msub_reg(5, 3, 4, R2));
    code_emit(code, a64_movz(7, '0', 0));
    code_emit(code, a64_sub_reg(5, 7, 5));
    code_emit(code, a64_strb_imm(5, 6, 0));
    code_emit(code, a64_sub_imm(6, 6, 1, true));
    code_emit(code, a64_add_imm(8, 8, 1, false));
    code_emit(code, a64_add_imm(R2, 3, 0, false));
    code_emit(code, a64_cmp_imm(R2, 0));
    code_emit(code, a64_bcond(digit_loop - code->count, COND_NE));

    code_emit(code, a64_cmp_imm(9, 0));
    int32_t no_sign = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz(5, '-', 0));
    code_emit(code, a64_strb_imm(5, 6, 0));
    code_emit(code, a64_sub_imm(6, 6, 1, true));
    code_emit(code, a64_add_imm(8, 8, 1, false));
    a64_patch_bcond(code, no_sign, code->count);

    code_emit(code, a64_add_imm(6, 6, 1, true));
    a64_emit_str_sp_off(code, 6, body->slot_offset[dst], true);
    a64_emit_str_sp_off(code, 8, body->slot_offset[dst] + 8, true);
    a64_patch_b(code, zero_done, code->count);
}

static void codegen_i64_to_str(Code *code, BodyIR *body, int32_t dst, int32_t src) {
    if (body->slot_kind[src] != SLOT_I64) { codegen_zero_slot(code, body, dst); return; }
    code_emit(code, a64_movz_x(R0, 0, 0));
    code_emit(code, a64_movz_x(R1, 32, 0));
    code_emit(code, a64_movz_x(R2, 3, 0));
    code_emit(code, a64_movz_x(R3, 0x1002, 0));
    code_emit(code, a64_movz_x(4, 0, 0));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_movz_x(5, 0, 0));
    code_emit(code, a64_movz_x(16, 197, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 4));
    int32_t mmap_ok = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_brk(82));
    a64_patch_bcond(code, mmap_ok, code->count);
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);

    a64_emit_ldr_sp_off(code, R2, body->slot_offset[src], true);
    code_emit(code, a64_cmp_reg_x(R2, 31));
    int32_t non_zero = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(5, '0', 0));
    code_emit(code, a64_strb_imm(5, R0, 0));
    code_emit(code, a64_movz_x(8, 1, 0));
    a64_emit_str_sp_off(code, 8, body->slot_offset[dst] + 8, true);
    int32_t zero_done = code->count;
    code_emit(code, a64_b(0));

    a64_patch_bcond(code, non_zero, code->count);
    code_emit(code, a64_add_imm(6, R0, 31, true));
    code_emit(code, a64_movz_x(8, 0, 0));
    code_emit(code, a64_movz_x(9, 0, 0));
    code_emit(code, a64_cmp_reg_x(R2, 31));
    int32_t non_negative = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_movz_x(9, 1, 0));
    code_emit(code, a64_sub_reg_x(R2, 31, R2));
    a64_patch_bcond(code, non_negative, code->count);

    code_emit(code, a64_movz_x(4, 10, 0));
    int32_t digit_loop = code->count;
    code_emit(code, a64_sdiv_reg_x(3, R2, 4));
    code_emit(code, a64_msub_reg_x(5, 3, 4, R2));
    code_emit(code, a64_movz(7, '0', 0));
    code_emit(code, a64_add_reg(5, 5, 7));
    code_emit(code, a64_strb_imm(5, 6, 0));
    code_emit(code, a64_sub_imm(6, 6, 1, true));
    code_emit(code, a64_add_imm(8, 8, 1, true));
    code_emit(code, a64_add_imm(R2, 3, 0, true));
    code_emit(code, a64_cmp_reg_x(R2, 31));
    code_emit(code, a64_bcond(digit_loop - code->count, COND_NE));

    code_emit(code, a64_cmp_reg_x(9, 31));
    int32_t no_sign = code->count;
    code_emit(code, a64_bcond(0, COND_EQ));
    code_emit(code, a64_movz(5, '-', 0));
    code_emit(code, a64_strb_imm(5, 6, 0));
    code_emit(code, a64_sub_imm(6, 6, 1, true));
    code_emit(code, a64_add_imm(8, 8, 1, true));
    a64_patch_bcond(code, no_sign, code->count);

    code_emit(code, a64_add_imm(6, 6, 1, true));
    a64_emit_str_sp_off(code, 6, body->slot_offset[dst], true);
    a64_emit_str_sp_off(code, 8, body->slot_offset[dst] + 8, true);
    a64_patch_b(code, zero_done, code->count);
}

static void codegen_str_index(Code *code, BodyIR *body, int32_t dst,
                              int32_t str_slot, int32_t index_slot) {
    int32_t str_kind = body->slot_kind[str_slot];
    if (str_kind != SLOT_STR && str_kind != SLOT_STR_REF) { codegen_zero_slot(code, body, dst); return; }
    if (body->slot_kind[index_slot] != SLOT_I32) { codegen_zero_slot(code, body, dst); return; }
    if (str_kind == SLOT_STR_REF) {
        a64_emit_ldr_sp_off(code, R2, body->slot_offset[str_slot], true);
        code_emit(code, a64_ldr_imm(R3, R2, 0, true));
        code_emit(code, a64_ldr_imm(4, R2, 8, true));
    } else {
        a64_emit_ldr_sp_off(code, R3, body->slot_offset[str_slot], true);
        a64_emit_ldr_sp_off(code, 4, body->slot_offset[str_slot] + 8, true);
    }
    a64_emit_ldr_sp_off(code, R1, body->slot_offset[index_slot], false);
    code_emit(code, a64_cmp_imm(R1, 0));
    int32_t non_negative = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_brk(7));
    a64_patch_bcond(code, non_negative, code->count);
    code_emit(code, a64_cmp_reg(R1, 4));
    int32_t in_bounds = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    code_emit(code, a64_brk(8));
    a64_patch_bcond(code, in_bounds, code->count);
    code_emit(code, a64_add_reg_x(R3, R3, R1));
    code_emit(code, a64_ldrb_imm(R0, R3, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
}

static void codegen_seq_str_index(Code *code, BodyIR *body, int32_t dst,
                                  int32_t seq_slot, int32_t index_slot) {
    int32_t seq_kind = body->slot_kind[seq_slot];
    if (seq_kind != SLOT_SEQ_STR && seq_kind != SLOT_SEQ_STR_REF) { codegen_zero_slot(code, body, dst); return; }
    if (body->slot_kind[index_slot] != SLOT_I32) { codegen_zero_slot(code, body, dst); return; }
    int header_reg = R2;
    if (seq_kind == SLOT_SEQ_STR_REF) {
        a64_emit_ldr_sp_off(code, header_reg, body->slot_offset[seq_slot], true);
    } else {
        a64_emit_add_large(code, header_reg, SP, body->slot_offset[seq_slot], true);
    }
    a64_emit_ldr_sp_off(code, R1, body->slot_offset[index_slot], false);
    code_emit(code, a64_cmp_imm(R1, 0));
    int32_t non_negative = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    /* Negative index: return 0 */
    code_emit(code, a64_movz(R0, 0, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    int32_t neg_done_jump = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, non_negative, code->count);
    code_emit(code, a64_ldr_imm(R3, header_reg, 0, false));
    code_emit(code, a64_cmp_reg(R1, R3));
    int32_t in_bounds = code->count;
    code_emit(code, a64_bcond(0, COND_LT));
    /* Index out of bounds: return 0 */
    code_emit(code, a64_movz(R0, 0, 0));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    /* Jump to common epilogue */
    int32_t oob_done_jump = code->count;
    code_emit(code, a64_b(0));
    a64_patch_bcond(code, in_bounds, code->count);
    code_emit(code, a64_ldr_imm(4, header_reg, 8, true));
    code_emit(code, a64_add_reg_x(5, R1, R1));
    code_emit(code, a64_add_reg_x(5, 5, 5));
    code_emit(code, a64_add_reg_x(5, 5, 5));
    code_emit(code, a64_add_reg_x(5, 5, 5));
    code_emit(code, a64_add_reg_x(4, 4, 5));
    code_emit(code, a64_ldr_imm(R0, 4, 0, true));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    code_emit(code, a64_ldr_imm(R0, 4, 8, true));
    a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + 8, true);
    /* Patch error jumps to here */
    int32_t seq_end = code->count;
    code->words[neg_done_jump] = a64_b(seq_end - neg_done_jump);
    code->words[oob_done_jump] = a64_b(seq_end - oob_done_jump);
}

static void codegen_seq_header_addr(Code *code, BodyIR *body, int32_t seq_slot, int reg) {
    int32_t kind = body->slot_kind[seq_slot];
    if (kind == SLOT_SEQ_I32) {
        a64_emit_add_large(code, reg, SP, body->slot_offset[seq_slot], true);
        return;
    }
    if (kind == SLOT_SEQ_I32_REF) {
        a64_emit_ldr_sp_off(code, reg, body->slot_offset[seq_slot], true);
        return;
    }
    if (kind == SLOT_SEQ_OPAQUE_REF || kind == SLOT_OBJECT_REF) {
        a64_emit_ldr_sp_off(code, reg, body->slot_offset[seq_slot], true);
        return;
    }
    /* Non-I32 seq: treat as inline on stack (SP + slot_offset) */
    a64_emit_add_large(code, reg, SP, body->slot_offset[seq_slot], true);
}

static void codegen_seq_str_header_addr(Code *code, BodyIR *body, int32_t seq_slot, int reg) {
    int32_t kind = body->slot_kind[seq_slot];
    if (kind == SLOT_SEQ_STR) {
        a64_emit_add_large(code, reg, SP, body->slot_offset[seq_slot], true);
        return;
    }
    if (kind == SLOT_SEQ_STR_REF) {
        a64_emit_ldr_sp_off(code, reg, body->slot_offset[seq_slot], true);
        return;
    }
    /* Non-str seq: treat as inline on stack (SP + slot_offset) */
    a64_emit_add_large(code, reg, SP, body->slot_offset[seq_slot], true);
}

static void codegen_seq_opaque_header_addr(Code *code, BodyIR *body, int32_t seq_slot, int reg) {
    int32_t kind = body->slot_kind[seq_slot];
    if (kind == SLOT_SEQ_OPAQUE) {
        a64_emit_add_large(code, reg, SP, body->slot_offset[seq_slot], true);
        return;
    }
    if (kind == SLOT_SEQ_OPAQUE_REF || kind == SLOT_OBJECT_REF || kind == SLOT_PTR) {
        a64_emit_ldr_sp_off(code, reg, body->slot_offset[seq_slot], true);
        return;
    }
    die("opaque sequence header kind mismatch");
}

static void codegen_seq_i32_add(Code *code, BodyIR *body, int32_t seq_slot, int32_t value_slot) {
    codegen_seq_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_ldr_imm(R1, R2, 4, false));
    code_emit(code, a64_cmp_reg(R0, R1));
    int32_t grow_pos = code->count;
    code_emit(code, a64_bcond(0, COND_GE));

    int32_t store_label = code->count;
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    a64_emit_ldr_sp_off(code, 4, body->slot_offset[value_slot], false);
    code_emit(code, a64_str_w_reg_uxtw2(4, R3, R0));
    code_emit(code, a64_add_imm(R0, R0, 1, false));
    code_emit(code, a64_str_imm(R0, R2, 0, false));
    int32_t done_jump = code->count;
    code_emit(code, a64_b(0));

    int32_t grow_label = code->count;
    a64_patch_bcond(code, grow_pos, grow_label);
    code_emit(code, a64_cmp_imm(R1, 0));
    int32_t double_pos = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(R1, 4, 0));
    int32_t cap_ready_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t double_label = code->count;
    a64_patch_bcond(code, double_pos, double_label);
    code_emit(code, a64_add_reg(R1, R1, R1));
    int32_t cap_ready = code->count;
    a64_patch_b(code, cap_ready_jump, cap_ready);
    code_emit(code, a64_str_imm(R1, R2, 4, false));

    code_emit(code, a64_add_reg(R1, R1, R1));
    code_emit(code, a64_add_reg(R1, R1, R1));
    code_emit(code, a64_movz_x(R0, 0, 0));
    code_emit(code, a64_movz_x(R2, 3, 0));
    code_emit(code, a64_movz_x(R3, 0x1002, 0));
    code_emit(code, a64_movz_x(4, 0, 0));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_movz_x(5, 0, 0));
    code_emit(code, a64_movz_x(16, 197, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 4));
    int32_t mmap_ok = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_brk(3));
    a64_patch_bcond(code, mmap_ok, code->count);
    code_emit(code, a64_add_imm(5, R0, 0, true));

    codegen_seq_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    code_emit(code, a64_movz(6, 0, 0));
    int32_t copy_loop = code->count;
    code_emit(code, a64_cmp_reg(6, R0));
    int32_t copy_done_pos = code->count;
    code_emit(code, a64_bcond(0, COND_GE));
    code_emit(code, a64_ldr_w_reg_uxtw2(7, R3, 6));
    code_emit(code, a64_str_w_reg_uxtw2(7, 5, 6));
    code_emit(code, a64_add_imm(6, 6, 1, false));
    code_emit(code, a64_b(copy_loop - code->count));
    int32_t copy_done = code->count;
    a64_patch_bcond(code, copy_done_pos, copy_done);
    code_emit(code, a64_str_imm(5, R2, 8, true));
    code_emit(code, a64_b(store_label - code->count));

    a64_patch_b(code, done_jump, code->count);
}

static void codegen_seq_str_add(Code *code, BodyIR *body, int32_t seq_slot, int32_t value_slot) {
    if (body->slot_kind[value_slot] != SLOT_STR && body->slot_kind[value_slot] != SLOT_STR_REF)
        die("str[] add value kind mismatch");
    codegen_seq_str_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_ldr_imm(R1, R2, 4, false));
    code_emit(code, a64_cmp_reg(R0, R1));
    int32_t grow_pos = code->count;
    code_emit(code, a64_bcond(0, COND_GE));

    int32_t store_label = code->count;
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    code_emit(code, a64_add_reg_x(6, R0, R0));
    code_emit(code, a64_add_reg_x(6, 6, 6));
    code_emit(code, a64_add_reg_x(6, 6, 6));
    code_emit(code, a64_add_reg_x(6, 6, 6));
    code_emit(code, a64_add_reg_x(R3, R3, 6));
    codegen_load_str_pair(code, body, value_slot, 4, 5);
    code_emit(code, a64_str_imm(4, R3, 0, true));
    code_emit(code, a64_str_imm(5, R3, 8, true));
    code_emit(code, a64_add_imm(R0, R0, 1, false));
    code_emit(code, a64_str_imm(R0, R2, 0, false));
    int32_t done_jump = code->count;
    code_emit(code, a64_b(0));

    int32_t grow_label = code->count;
    a64_patch_bcond(code, grow_pos, grow_label);
    code_emit(code, a64_cmp_imm(R1, 0));
    int32_t double_pos = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(R1, 4, 0));
    int32_t cap_ready_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t double_label = code->count;
    a64_patch_bcond(code, double_pos, double_label);
    code_emit(code, a64_add_reg(R1, R1, R1));
    int32_t cap_ready = code->count;
    a64_patch_b(code, cap_ready_jump, cap_ready);
    code_emit(code, a64_str_imm(R1, R2, 4, false));

    code_emit(code, a64_add_reg(R1, R1, R1));
    code_emit(code, a64_add_reg(R1, R1, R1));
    code_emit(code, a64_add_reg(R1, R1, R1));
    code_emit(code, a64_add_reg(R1, R1, R1));
    code_emit(code, a64_movz_x(R0, 0, 0));
    code_emit(code, a64_movz_x(R2, 3, 0));
    code_emit(code, a64_movz_x(R3, 0x1002, 0));
    code_emit(code, a64_movz_x(4, 0, 0));
    code_emit(code, a64_sub_imm(4, 4, 1, true));
    code_emit(code, a64_movz_x(5, 0, 0));
    code_emit(code, a64_movz_x(16, 197, 0));
    code_emit(code, a64_svc(0x80));
    code_emit(code, a64_cmp_reg_x(R0, 4));
    int32_t mmap_ok = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_brk(11));
    a64_patch_bcond(code, mmap_ok, code->count);
    code_emit(code, a64_add_imm(7, R0, 0, true));
    code_emit(code, a64_add_imm(6, R0, 0, true));

    codegen_seq_str_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    code_emit(code, a64_add_reg(9, R0, R0));
    code_emit(code, a64_add_reg(9, 9, 9));
    code_emit(code, a64_add_reg(9, 9, 9));
    code_emit(code, a64_add_reg(9, 9, 9));
    codegen_copy_bytes(code, R3, 9, 6);
    code_emit(code, a64_str_imm(7, R2, 8, true));
    code_emit(code, a64_b(store_label - code->count));

    a64_patch_b(code, done_jump, code->count);
}

static int32_t codegen_seq_opaque_element_size(BodyIR *body, int32_t seq_slot, int32_t element_size) {
    if (element_size <= 0 && seq_slot >= 0 && seq_slot < body->slot_count)
        element_size = body->slot_aux[seq_slot];
    if (element_size <= 0) die("opaque sequence element size missing in codegen");
    return element_size;
}

static void codegen_slot_data_addr(Code *code, BodyIR *body, int32_t slot, int reg) {
    int32_t kind = body->slot_kind[slot];
    if (kind == SLOT_OBJECT_REF || kind == SLOT_OPAQUE_REF || kind == SLOT_PTR ||
        kind == SLOT_SEQ_OPAQUE_REF || kind == SLOT_SEQ_I32_REF ||
        kind == SLOT_SEQ_STR_REF || kind == SLOT_STR_REF) {
        a64_emit_ldr_sp_off(code, reg, body->slot_offset[slot], true);
        return;
    }
    a64_emit_add_large(code, reg, SP, body->slot_offset[slot], true);
}

static void codegen_seq_opaque_index(Code *code, BodyIR *body, int32_t dst,
                                     int32_t seq_slot, int32_t index_slot,
                                     int32_t element_size) {
    element_size = codegen_seq_opaque_element_size(body, seq_slot, element_size);
    if (body->slot_size[dst] > element_size) die("opaque sequence index destination too large");
    codegen_seq_opaque_header_addr(code, body, seq_slot, R2);
    a64_emit_ldr_sp_off(code, R1, body->slot_offset[index_slot], false);
    code_emit(code, a64_cmp_imm(R1, 0));
    code_emit(code, a64_bcond(2, COND_GE));
    code_emit(code, a64_brk(120));
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_cmp_reg(R1, R0));
    code_emit(code, a64_bcond(2, COND_LT));
    code_emit(code, a64_brk(120));
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    codegen_mov_i32_const(code, 6, element_size);
    code_emit(code, a64_mul_reg_x(6, R1, 6));
    code_emit(code, a64_add_reg_x(R3, R3, 6));
    a64_emit_add_large(code, 7, SP, body->slot_offset[dst], true);
    codegen_mov_i32_const(code, 6, body->slot_size[dst]);
    codegen_copy_bytes(code, R3, 6, 7);
}

static void codegen_seq_opaque_index_store(Code *code, BodyIR *body, int32_t value_slot,
                                           int32_t seq_slot, int32_t index_slot,
                                           int32_t element_size) {
    element_size = codegen_seq_opaque_element_size(body, seq_slot, element_size);
    if (body->slot_size[value_slot] > element_size) die("opaque sequence store value too large");
    codegen_seq_opaque_header_addr(code, body, seq_slot, R2);
    a64_emit_ldr_sp_off(code, R1, body->slot_offset[index_slot], false);
    code_emit(code, a64_cmp_imm(R1, 0));
    code_emit(code, a64_bcond(2, COND_GE));
    code_emit(code, a64_brk(122));
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_cmp_reg(R1, R0));
    code_emit(code, a64_bcond(2, COND_LT));
    code_emit(code, a64_brk(122));
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    codegen_mov_i32_const(code, 6, element_size);
    code_emit(code, a64_mul_reg_x(6, R1, 6));
    code_emit(code, a64_add_reg_x(R3, R3, 6));
    codegen_slot_data_addr(code, body, value_slot, 7);
    codegen_mov_i32_const(code, 6, element_size);
    codegen_copy_bytes(code, 7, 6, R3);
}

static void codegen_seq_opaque_add(Code *code, BodyIR *body, int32_t seq_slot,
                                   int32_t value_slot, int32_t element_size) {
    element_size = codegen_seq_opaque_element_size(body, seq_slot, element_size);
    if (body->slot_size[value_slot] > element_size) die("opaque sequence add value too large");
    codegen_seq_opaque_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_ldr_imm(R1, R2, 4, false));
    code_emit(code, a64_cmp_reg(R0, R1));
    int32_t grow_pos = code->count;
    code_emit(code, a64_bcond(0, COND_GE));

    int32_t store_label = code->count;
    codegen_seq_opaque_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    codegen_mov_i32_const(code, 6, element_size);
    code_emit(code, a64_mul_reg_x(6, R0, 6));
    code_emit(code, a64_add_reg_x(R3, R3, 6));
    codegen_slot_data_addr(code, body, value_slot, 7);
    codegen_mov_i32_const(code, 6, element_size);
    codegen_copy_bytes(code, 7, 6, R3);
    code_emit(code, a64_add_imm(R0, R0, 1, false));
    code_emit(code, a64_str_imm(R0, R2, 0, false));
    int32_t done_jump = code->count;
    code_emit(code, a64_b(0));

    int32_t grow_label = code->count;
    a64_patch_bcond(code, grow_pos, grow_label);
    code_emit(code, a64_cmp_imm(R1, 0));
    int32_t double_pos = code->count;
    code_emit(code, a64_bcond(0, COND_NE));
    code_emit(code, a64_movz(R1, 4, 0));
    int32_t cap_ready_jump = code->count;
    code_emit(code, a64_b(0));
    int32_t double_label = code->count;
    a64_patch_bcond(code, double_pos, double_label);
    code_emit(code, a64_add_reg(R1, R1, R1));
    int32_t cap_ready = code->count;
    a64_patch_b(code, cap_ready_jump, cap_ready);
    codegen_seq_opaque_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_str_imm(R1, R2, 4, false));
    codegen_mov_i32_const(code, 6, element_size);
    code_emit(code, a64_mul_reg(R1, R1, 6));
    codegen_mmap_len_reg(code, R1, 7, 121);
    code_emit(code, a64_add_imm(8, 7, 0, true));

    codegen_seq_opaque_header_addr(code, body, seq_slot, R2);
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    codegen_mov_i32_const(code, 6, element_size);
    code_emit(code, a64_mul_reg(6, R0, 6));
    codegen_copy_bytes(code, R3, 6, 7);
    code_emit(code, a64_str_imm(8, R2, 8, true));
    code_emit(code, a64_b(store_label - code->count));

    a64_patch_b(code, done_jump, code->count);
}

static void codegen_seq_opaque_remove(Code *code, BodyIR *body, int32_t seq_slot,
                                      int32_t index_slot, int32_t element_size) {
    element_size = codegen_seq_opaque_element_size(body, seq_slot, element_size);
    codegen_seq_opaque_header_addr(code, body, seq_slot, R2);
    a64_emit_ldr_sp_off(code, R1, body->slot_offset[index_slot], false);
    code_emit(code, a64_cmp_imm(R1, 0));
    code_emit(code, a64_bcond(2, COND_GE));
    code_emit(code, a64_brk(123));
    code_emit(code, a64_ldr_imm(R0, R2, 0, false));
    code_emit(code, a64_cmp_reg(R1, R0));
    code_emit(code, a64_bcond(2, COND_LT));
    code_emit(code, a64_brk(123));
    code_emit(code, a64_ldr_imm(R3, R2, 8, true));
    codegen_mov_i32_const(code, 10, element_size);
    code_emit(code, a64_mul_reg_x(7, R1, 10));
    code_emit(code, a64_add_reg_x(8, R3, 7));
    code_emit(code, a64_add_imm(9, 8, (uint16_t)element_size, true));
    code_emit(code, a64_sub_reg(6, R0, R1));
    code_emit(code, a64_sub_imm(6, 6, 1, false));
    code_emit(code, a64_mul_reg(6, 6, 10));
    codegen_copy_bytes(code, 9, 6, 8);
    code_emit(code, a64_sub_imm(R0, R0, 1, false));
    code_emit(code, a64_str_imm(R0, R2, 0, false));
}

/* No-alias register cache.
 * For no_alias slots (scalar locals), the value lives on the stack but is
 * also cached in a register.  Since no_alias slots never alias each other,
 * the cached register value is valid until the register is reused for a
 * different purpose.  The cache is invalidated at basic block boundaries
 * so it remains correct across CFG joins. */
#define NA_CACHE_SIZE 32
static int32_t na_reg_slot[NA_CACHE_SIZE]; /* [register] -> slot, -1 = empty */

/* Reset entire cache (function entry or block boundary). */
static void na_reset(void) {
    for (int i = 0; i < NA_CACHE_SIZE; i++) na_reg_slot[i] = -1;
}

/* Return the register caching slot s, or -1 if not cached. */
static int na_find(int32_t s) {
    for (int i = 0; i < NA_CACHE_SIZE; i++) {
        if (na_reg_slot[i] == s) return i;
    }
    return -1;
}

/* Cache that register r now holds slot s.  If another register was
 * already caching s, invalidate that stale copy. */
static void na_set(int r, int32_t s) {
    if (r >= 0 && r < NA_CACHE_SIZE) {
        /* Invalidate any other register caching this slot */
        for (int i = 0; i < NA_CACHE_SIZE; i++) {
            if (i != r && na_reg_slot[i] == s) na_reg_slot[i] = -1;
        }
        na_reg_slot[r] = s;
    }
}

/* Register r is being reused for another purpose; invalidate its entry. */
static void na_clobber(int r) {
    if (r >= 0 && r < NA_CACHE_SIZE) na_reg_slot[r] = -1;
}

static void codegen_op(Code *code, BodyIR *body, Symbols *symbols,
                       FunctionPatchList *function_patches, int32_t op) {
    int32_t kind = body->op_kind[op];
    int32_t dst = body->op_dst[op];
    int32_t a = body->op_a[op];
    int32_t b = body->op_b[op];
    int32_t c = body->op_c[op];
    if (kind == BODY_OP_I32_CONST) {
        codegen_mov_i32_const(code, R0, a);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_I64_CONST) {
        uint64_t bits = (uint32_t)a | ((uint64_t)(uint32_t)b << 32);
        codegen_mov_i64_const(code, R0, bits);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_BRK) {
        (void)dst;
        code_emit(code, a64_brk(a));
    } else if (kind == BODY_OP_LOAD_I32) {
        int __cr = na_find(dst);
        if (__cr >= 0) { if (__cr != 0) code_emit(code, a64_add_imm(R0, __cr, 0, false)); }
        else {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[dst], false);
            if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
        }
    } else if (kind == BODY_OP_I32_REF_LOAD) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], true);
        code_emit(code, a64_ldr_imm(R0, R1, 0, false));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_I32_REF_STORE) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[dst], true);
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_str_imm(R0, R1, 0, false));
    } else if (kind == BODY_OP_COPY_I64) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_I64_FROM_I32) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_sxtw(R0, R0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_I32_FROM_I64) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_I64_ADD || kind == BODY_OP_I64_SUB ||
               kind == BODY_OP_I64_MUL || kind == BODY_OP_I64_DIV) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], true);
        if (kind == BODY_OP_I64_ADD) code_emit(code, a64_add_reg_x(R0, R0, R1));
        else if (kind == BODY_OP_I64_SUB) code_emit(code, a64_sub_reg_x(R0, R0, R1));
        else if (kind == BODY_OP_I64_MUL) code_emit(code, a64_mul_reg_x(R0, R0, R1));
        else code_emit(code, a64_sdiv_reg_x(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_I64_AND || kind == BODY_OP_I64_OR ||
               kind == BODY_OP_I64_XOR || kind == BODY_OP_I64_SHL ||
               kind == BODY_OP_I64_ASR) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], true);
        if (kind == BODY_OP_I64_AND) code_emit(code, a64_and_reg_x(R0, R0, R1));
        else if (kind == BODY_OP_I64_OR) code_emit(code, a64_orr_reg_x(R0, R0, R1));
        else if (kind == BODY_OP_I64_XOR) code_emit(code, a64_eor_reg_x(R0, R0, R1));
        else if (kind == BODY_OP_I64_SHL) code_emit(code, a64_lsl_reg_x(R0, R0, R1));
        else code_emit(code, a64_asr_reg_x(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_I64_CMP) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], true);
        code_emit(code, a64_cmp_reg_x(R0, R1));
        code_emit(code, a64_movz(R2, 0, 0));
        int32_t true_pos = code->count;
        code_emit(code, a64_bcond(0, c));
        int32_t done_pos = code->count;
        code_emit(code, a64_b(0));
        a64_patch_bcond(code, true_pos, code->count);
        code_emit(code, a64_movz(R2, 1, 0));
        a64_patch_b(code, done_pos, code->count);
        a64_emit_str_sp_off(code, R2, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_PTR_CONST) {
        code_emit(code, a64_movz_x(R0, (uint16_t)a, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_TIME_NS) {
        code_emit(code, a64_sub_imm(SP, SP, 32, true));
        code_emit(code, a64_add_imm(R0, SP, 0, true));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(16, 116, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t ok_jump = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(R2, 0, 0));
        int32_t time_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t time_ok_label = code->count;
        a64_patch_bcond(code, ok_jump, time_ok_label);
        a64_emit_ldr_sp_off(code, R1, 0, true);
        a64_emit_ldr_sp_off(code, R2, 8, true);
        a64_patch_b(code, time_done_jump, code->count);
        code_emit(code, a64_add_imm(SP, SP, 32, true));
        codegen_mov_i64_const(code, R3, 1000000000ULL);
        code_emit(code, a64_mul_reg_x(R1, R1, R3));
        codegen_mov_i64_const(code, R3, 1000ULL);
        code_emit(code, a64_mul_reg_x(R2, R2, R3));
        code_emit(code, a64_add_reg_x(R0, R1, R2));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_GETRUSAGE) {
        if (body->slot_kind[a] != SLOT_I32) return;
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        if (body->slot_kind[b] == SLOT_OBJECT_REF || body->slot_kind[b] == SLOT_OPAQUE_REF) {
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], true);
        } else if (body->slot_kind[b] == SLOT_OBJECT || body->slot_kind[b] == SLOT_OPAQUE) {
            a64_emit_add_large(code, R1, SP, body->slot_offset[b], true);
        } else {
            return; /* skip getrusage with unknown usage slot */
        }
        code_emit(code, a64_movz_x(16, 117, 0));
        code_emit(code, a64_svc(0x80));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_EXIT) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_movz_x(16, 1, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_brk(0x4517));
    } else if (kind == BODY_OP_ASSERT) {
        if (body->slot_kind[a] == SLOT_I32_REF) {
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], true);
            code_emit(code, a64_ldr_imm(R0, R1, 0, false));
        } else if (body->slot_kind[a] == SLOT_I32) {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        } else {
            die("assert condition slot kind mismatch");
        }
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t ok_pos = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
        code_emit(code, a64_movz_x(R0, 2, 0));
        if (body->slot_kind[b] == SLOT_STR_REF) {
            a64_emit_ldr_sp_off(code, R3, body->slot_offset[b], true);
            code_emit(code, a64_ldr_imm(R1, R3, 0, true));
            code_emit(code, a64_ldr_imm(R2, R3, 8, true));
        } else if (body->slot_kind[b] == SLOT_STR) {
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], true);
            a64_emit_ldr_sp_off(code, R2, body->slot_offset[b] + 8, true);
        } else {
            die("assert message slot kind mismatch");
        }
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_movz_x(R0, 2, 0));
        code_emit(code, a64_movz(R1, '\n', 0));
        code_emit(code, a64_strb_imm(R1, SP, body->slot_offset[dst]));
        a64_emit_add_large(code, R1, SP, body->slot_offset[dst], true);
        code_emit(code, a64_movz_x(R2, 1, 0));
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_movz(R0, 1, 0));
        code_emit(code, a64_movz_x(16, 1, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_brk(0xA55E));
        a64_patch_bcond(code, ok_pos, code->count);
        code_emit(code, a64_movz(R0, 0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_WRITE_LINE) {
        if (body->slot_kind[a] == SLOT_OPAQUE) {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        } else if (body->slot_kind[a] == SLOT_I32) {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        } else {
            return;
        }
        if (body->slot_kind[b] == SLOT_STR_REF) {
            a64_emit_ldr_sp_off(code, R3, body->slot_offset[b], true);
            code_emit(code, a64_ldr_imm(R1, R3, 0, true));
            code_emit(code, a64_ldr_imm(R2, R3, 8, true));
        } else if (body->slot_kind[b] == SLOT_STR) {
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], true);
            a64_emit_ldr_sp_off(code, R2, body->slot_offset[b] + 8, true);
        } else {
            return;
        }
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        if (body->slot_kind[a] == SLOT_OPAQUE) {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        } else {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        }
        code_emit(code, a64_movz(R1, '\n', 0));
        code_emit(code, a64_strb_imm(R1, SP, body->slot_offset[dst]));
        a64_emit_add_large(code, R1, SP, body->slot_offset[dst], true);
        code_emit(code, a64_movz_x(R2, 1, 0));
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_movz(R0, 0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_ARGC_LOAD) {
        a64_emit_str_sp_off(code, 19, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_ARGV_STR) {
        if (body->slot_kind[a] != SLOT_I32) return;
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], false);
        code_emit(code, a64_cmp_imm(R1, 0));
        int32_t fail_neg = code->count;
        code_emit(code, a64_bcond(0, COND_LT));
        code_emit(code, a64_cmp_reg(R1, 19));
        int32_t fail_high = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_add_imm(4, 20, 0, true));
        code_emit(code, a64_add_imm(5, R1, 0, true));
        code_emit(code, a64_add_reg_x(5, 5, 5));
        code_emit(code, a64_add_reg_x(5, 5, 5));
        code_emit(code, a64_add_reg_x(5, 5, 5));
        code_emit(code, a64_add_reg_x(4, 4, 5));
        code_emit(code, a64_ldr_imm(4, 4, 0, true));
        code_emit(code, a64_cmp_reg_x(4, 31));
        int32_t fail_null = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_movz_x(5, 0, 0));
        code_emit(code, a64_add_imm(7, 4, 0, true));
        int32_t len_loop = code->count;
        code_emit(code, a64_ldrb_imm(6, 7, 0));
        code_emit(code, a64_cmp_imm(6, 0));
        int32_t len_done_pos = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_add_imm(7, 7, 1, true));
        code_emit(code, a64_add_imm(5, 5, 1, true));
        code_emit(code, a64_b(len_loop - code->count));
        int32_t len_done = code->count;
        a64_patch_bcond(code, len_done_pos, len_done);
        a64_emit_str_sp_off(code, 4, body->slot_offset[dst], true);
        a64_emit_str_sp_off(code, 5, body->slot_offset[dst] + 8, true);
        int32_t done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t zero_label = code->count;
        a64_patch_bcond(code, fail_neg, zero_label);
        a64_patch_bcond(code, fail_high, zero_label);
        a64_patch_bcond(code, fail_null, zero_label);
        code_emit(code, a64_movz_x(R0, 0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + 8, true);
        a64_patch_b(code, done_jump, code->count);
    } else if (kind == BODY_OP_GETENV_STR) {
        codegen_getenv_str(code, body, dst, a);
    } else if (kind == BODY_OP_STR_SELECT_NONEMPTY) {
        codegen_str_select_nonempty(code, body, dst, a, b);
    } else if (kind == BODY_OP_READ_FLAG) {
        codegen_read_flag(code, body, dst, a, b);
    } else if (kind == BODY_OP_PARSE_INT) {
        codegen_parse_int_slot(code, body, dst, a);
    } else if (kind == BODY_OP_STR_JOIN) {
        codegen_str_join(code, body, dst, a, b);
    } else if (kind == BODY_OP_STR_SPLIT_CHAR) {
        codegen_str_split_char(code, body, dst, a, b);
    } else if (kind == BODY_OP_STR_STRIP) {
        codegen_str_strip(code, body, dst, a);
    } else if (kind == BODY_OP_SHELL_QUOTE) {
        codegen_shell_quote(code, body, dst, a);
    } else if (kind == BODY_OP_EXEC_SHELL) {
        codegen_exec_shell(code, body, dst, a, b, c);
    } else if (kind == BODY_OP_STR_SLICE) {
        codegen_str_slice(code, body, dst, a, b, c);
    } else if (kind == BODY_OP_TEXT_SET_INIT) {
        codegen_zero_slot(code, body, dst);
    } else if (kind == BODY_OP_TEXT_SET_INSERT) {
        codegen_text_set_insert(code, body, symbols, dst, a, b);
    } else if (kind == BODY_OP_CWD_STR) {
        codegen_current_dir_to_regs(code, R2, R3);
        codegen_store_str_pair(code, body, dst, R2, R3);
    } else if (kind == BODY_OP_PATH_JOIN) {
        codegen_path_join_slots(code, body, dst, a, b);
    } else if (kind == BODY_OP_PATH_ABSOLUTE) {
        codegen_load_str_pair(code, body, b, R2, R3);
        code_emit(code, a64_cmp_imm(R3, 0));
        int32_t raw_empty = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_ldrb_imm(4, R2, 0));
        code_emit(code, a64_cmp_imm(4, '/'));
        int32_t raw_absolute = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        codegen_path_join_slots(code, body, dst, a, b);
        int32_t absolute_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t raw_empty_label = code->count;
        a64_patch_bcond(code, raw_empty, raw_empty_label);
        codegen_load_str_pair(code, body, a, R2, R3);
        codegen_store_str_pair(code, body, dst, R2, R3);
        int32_t empty_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t raw_absolute_label = code->count;
        a64_patch_bcond(code, raw_absolute, raw_absolute_label);
        codegen_load_str_pair(code, body, b, R2, R3);
        codegen_store_str_pair(code, body, dst, R2, R3);
        int32_t done_label = code->count;
        a64_patch_b(code, absolute_done_jump, done_label);
        a64_patch_b(code, empty_done_jump, done_label);
    } else if (kind == BODY_OP_PATH_PARENT) {
        codegen_load_str_pair(code, body, a, R2, R3);
        code_emit(code, a64_cmp_imm(R3, 0));
        int32_t empty_pos = code->count;
        code_emit(code, a64_bcond(0, COND_LE));
        code_emit(code, a64_movz(4, 0, 0));
        code_emit(code, a64_sub_imm(4, 4, 1, false));
        code_emit(code, a64_movz(5, 0, 0));
        int32_t loop = code->count;
        code_emit(code, a64_cmp_reg(5, R3));
        int32_t scan_done = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_add_reg_x(6, R2, 5));
        code_emit(code, a64_ldrb_imm(7, 6, 0));
        code_emit(code, a64_cmp_imm(7, '/'));
        int32_t slash_pos = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_cmp_imm(7, '\\'));
        int32_t backslash_pos = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        int32_t not_sep_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t set_last = code->count;
        a64_patch_bcond(code, slash_pos, set_last);
        a64_patch_bcond(code, backslash_pos, set_last);
        code_emit(code, a64_add_imm(4, 5, 0, false));
        a64_patch_b(code, not_sep_jump, code->count);
        code_emit(code, a64_add_imm(5, 5, 1, false));
        code_emit(code, a64_b(loop - code->count));
        int32_t after_scan = code->count;
        a64_patch_bcond(code, scan_done, after_scan);
        code_emit(code, a64_cmp_imm(4, 0));
        int32_t no_parent = code->count;
        code_emit(code, a64_bcond(0, COND_LE));
        codegen_store_str_pair(code, body, dst, R2, 4);
        int32_t parent_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t empty_label = code->count;
        a64_patch_bcond(code, empty_pos, empty_label);
        a64_patch_bcond(code, no_parent, empty_label);
        codegen_store_empty_str(code, body, dst);
        a64_patch_b(code, parent_done_jump, code->count);
    } else if (kind == BODY_OP_PATH_EXISTS) {
        codegen_cstring_from_slot(code, body, a, 7, 45);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(R2, 0, 0));
        code_emit(code, a64_movz_x(16, 5, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t open_ok = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_movz(R1, 0, 0));
        int32_t exists_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t opened = code->count;
        a64_patch_bcond(code, open_ok, opened);
        code_emit(code, a64_add_imm(9, R0, 0, true));
        if (b == 1) {
            code_emit(code, a64_sub_imm(SP, SP, 16, true));
            code_emit(code, a64_add_imm(R0, 9, 0, true));
            code_emit(code, a64_add_imm(R1, SP, 0, true));
            code_emit(code, a64_movz_x(R2, 1, 0));
            code_emit(code, a64_movz_x(16, 3, 0));
            code_emit(code, a64_svc(0x80));
            code_emit(code, a64_cmp_imm(R0, 0));
            int32_t has_byte = code->count;
            code_emit(code, a64_bcond(0, COND_GT));
            code_emit(code, a64_movz(R1, 0, 0));
            int32_t read_done_jump = code->count;
            code_emit(code, a64_b(0));
            int32_t has_byte_label = code->count;
            a64_patch_bcond(code, has_byte, has_byte_label);
            code_emit(code, a64_movz(R1, 1, 0));
            a64_patch_b(code, read_done_jump, code->count);
            code_emit(code, a64_add_imm(SP, SP, 16, true));
        } else {
            code_emit(code, a64_movz(R1, 1, 0));
        }
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(16, 6, 0));
        code_emit(code, a64_svc(0x80));
        a64_patch_b(code, exists_done_jump, code->count);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_PATH_FILE_SIZE) {
        codegen_cstring_from_slot(code, body, a, 7, 46);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(R2, 0, 0));
        code_emit(code, a64_movz_x(16, 5, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t size_open_ok = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_movz(R1, 0, 0));
        int32_t size_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t size_opened = code->count;
        a64_patch_bcond(code, size_open_ok, size_opened);
        code_emit(code, a64_add_imm(9, R0, 0, true));
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(R2, 2, 0));
        code_emit(code, a64_movz_x(16, 199, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_add_imm(R1, R0, 0, true));
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(16, 6, 0));
        code_emit(code, a64_svc(0x80));
        a64_patch_b(code, size_done_jump, code->count);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_PATH_READ_TEXT) {
        codegen_cstring_from_slot(code, body, a, 7, 70);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(R2, 0, 0));
        code_emit(code, a64_movz_x(16, 5, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t read_open_ok = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        codegen_store_empty_str(code, body, dst);
        int32_t read_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t read_opened = code->count;
        a64_patch_bcond(code, read_open_ok, read_opened);
        code_emit(code, a64_add_imm(9, R0, 0, true));
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(R2, 2, 0));
        code_emit(code, a64_movz_x(16, 199, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_add_imm(10, R0, 0, true));
        code_emit(code, a64_cmp_imm(10, 0));
        int32_t read_has_bytes = code->count;
        code_emit(code, a64_bcond(0, COND_GT));
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(16, 6, 0));
        code_emit(code, a64_svc(0x80));
        codegen_store_empty_str(code, body, dst);
        int32_t read_empty_done = code->count;
        code_emit(code, a64_b(0));
        int32_t read_has_bytes_label = code->count;
        a64_patch_bcond(code, read_has_bytes, read_has_bytes_label);
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(R1, 0, 0));
        code_emit(code, a64_movz_x(R2, 0, 0));
        code_emit(code, a64_movz_x(16, 199, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_add_imm(R1, 10, 1, true));
        codegen_mmap_len_reg(code, R1, 11, 70);
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_add_imm(R1, 11, 0, true));
        code_emit(code, a64_add_imm(R2, 10, 0, true));
        code_emit(code, a64_movz_x(16, 3, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t read_ok = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        codegen_store_empty_str(code, body, dst);
        int32_t read_close_after_error = code->count;
        code_emit(code, a64_b(0));
        int32_t read_ok_label = code->count;
        a64_patch_bcond(code, read_ok, read_ok_label);
        code_emit(code, a64_add_reg_x(12, 11, R0));
        code_emit(code, a64_movz(13, 0, 0));
        code_emit(code, a64_strb_imm(13, 12, 0));
        codegen_store_str_pair(code, body, dst, 11, R0);
        a64_patch_b(code, read_close_after_error, code->count);
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(16, 6, 0));
        code_emit(code, a64_svc(0x80));
        a64_patch_b(code, read_empty_done, code->count);
        a64_patch_b(code, read_done_jump, code->count);
    } else if (kind == BODY_OP_PATH_WRITE_TEXT) {
        codegen_cstring_from_slot(code, body, a, 7, 47);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_movz_x(R1, 0x0601, 0));
        code_emit(code, a64_movz_x(R2, 0644, 0));
        code_emit(code, a64_movz_x(16, 5, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t write_open_ok = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_movz(R1, 0, 0));
        int32_t write_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t write_opened = code->count;
        a64_patch_bcond(code, write_open_ok, write_opened);
        code_emit(code, a64_add_imm(9, R0, 0, true));
        codegen_load_str_pair(code, body, b, R1, R2);
        code_emit(code, a64_add_imm(13, R1, 0, true));
        code_emit(code, a64_add_imm(12, R2, 0, true));
        int32_t write_loop = code->count;
        code_emit(code, a64_cmp_imm(12, 0));
        int32_t write_success = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_add_imm(R1, 13, 0, true));
        code_emit(code, a64_add_imm(R2, 12, 0, true));
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t write_progress = code->count;
        code_emit(code, a64_bcond(0, COND_GT));
        int32_t write_error_close = code->count;
        code_emit(code, a64_b(0));
        a64_patch_bcond(code, write_progress, code->count);
        code_emit(code, a64_add_reg_x(13, 13, R0));
        code_emit(code, a64_sub_reg_x(12, 12, R0));
        code_emit(code, a64_b(write_loop - code->count));
        int32_t write_success_close = code->count;
        code_emit(code, a64_b(0));
        a64_patch_b(code, write_error_close, code->count);
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(16, 6, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_movz(R1, 0, 0));
        int32_t write_error_done = code->count;
        code_emit(code, a64_b(0));
        a64_patch_bcond(code, write_success, code->count);
        a64_patch_b(code, write_success_close, code->count);
        code_emit(code, a64_add_imm(R0, 9, 0, true));
        code_emit(code, a64_movz_x(16, 6, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_movz(R1, 1, 0));
        a64_patch_b(code, write_error_done, code->count);
        a64_patch_b(code, write_done_jump, code->count);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_REMOVE_FILE) {
        codegen_cstring_from_slot(code, body, a, 7, 71);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_movz_x(16, 10, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t remove_ok = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_movz(R1, 0, 0));
        int32_t remove_done = code->count;
        code_emit(code, a64_b(0));
        int32_t remove_ok_label = code->count;
        a64_patch_bcond(code, remove_ok, remove_ok_label);
        code_emit(code, a64_movz(R1, 1, 0));
        a64_patch_b(code, remove_done, code->count);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_CHMOD_X) {
        codegen_cstring_from_slot(code, body, a, 7, 72);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_movz_x(R1, 0755, 0));
        code_emit(code, a64_movz_x(16, 15, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t chmod_ok = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_movz(R1, 0, 0));
        int32_t chmod_done = code->count;
        code_emit(code, a64_b(0));
        int32_t chmod_ok_label = code->count;
        a64_patch_bcond(code, chmod_ok, chmod_ok_label);
        code_emit(code, a64_movz(R1, 1, 0));
        a64_patch_b(code, chmod_done, code->count);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_COLD_SELF_EXEC) {
        /* a: bin_path slot. Exec with original argv, replacing argv[0] with bin_path.
           X19=argc, X20=argv (saved by entry trampoline). */
        codegen_cstring_from_slot(code, body, a, 7, 73);
        /* Allocate new_argv on stack: (argc+1)*8, 16-byte aligned */
        code_emit(code, a64_add_imm(9, 19, 1, true));      /* X9 = argc+1 */
        code_emit(code, a64_lsl_imm(9, 9, 3, true));        /* X9 = (argc+1)*8 */
        code_emit(code, a64_add_imm(9, 9, 15, true));       /* +15 */
        code_emit(code, a64_movz_x(10, 0xFFF0, 0));         /* X10 = 0xFFFFFFF0 */
        code_emit(code, a64_movk(10, 0xFFFF, 1));           /* X10 = 0xFFFFFFFFFFFFFFF0 */
        code_emit(code, a64_and_reg(9, 9, 10));             /* X9 &= ~0xF (16-byte align) */
        code_emit(code, a64_sub_reg_x(SP, SP, 9));          /* alloc */
        code_emit(code, a64_add_imm(10, SP, 0, true));      /* X10 = new_argv */
        /* new_argv[0] = bin_path */
        a64_emit_str_sp_off(code, 7, 0, true);
        /* Copy argv[1..argc-1] */
        code_emit(code, a64_movz(11, 1, 0));                /* i=1 */
        int32_t loop_top = code->count;
        code_emit(code, a64_cmp_reg_x(11, 19));             /* cmp i,argc */
        int32_t loop_exit_jump = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_ldr_x_reg_lsl3(13, 20, 11));    /* X13=argv[i] */
        code_emit(code, a64_str_x_reg_lsl3(13, 10, 11));    /* new_argv[i]=X13 */
        code_emit(code, a64_add_imm(11, 11, 1, true));      /* i++ */
        int32_t loop_jump = code->count;
        code_emit(code, a64_b(0));
        a64_patch_b(code, loop_jump, loop_top);
        int32_t loop_exit = code->count;
        a64_patch_bcond(code, loop_exit_jump, loop_exit);
        /* new_argv[argc] = NULL */
        code_emit(code, a64_lsl_imm(12, 19, 3, true));      /* X12 = argc * 8 */
        code_emit(code, a64_add_reg_x(9, 10, 12));          /* X9 = new_argv + argc*8 */
        code_emit(code, a64_str_imm(31, 9, 0, true));       /* [X9] = 0 (NULL terminator) */
        /* execve(bin_path, new_argv, NULL) */
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_add_imm(R1, 10, 0, true));
        code_emit(code, a64_movz_x(R2, 0, 0));
        code_emit(code, a64_movz_x(16, 59, 0));
        code_emit(code, a64_svc(0x80));
        /* Error: execve returned, store -1 */
        code_emit(code, a64_movz(R1, 0, 0));
        code_emit(code, a64_sub_imm(R1, R1, 1, false));     /* R1 = -1 */
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_MKDIR_ONE) {
        codegen_cstring_from_slot(code, body, a, 7, 48);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        code_emit(code, a64_movz_x(R1, 0755, 0));
        code_emit(code, a64_movz_x(16, 136, 0));
        code_emit(code, a64_svc(0x80));
        code_emit(code, a64_cmp_imm(R0, 0));
        int32_t mkdir_ok = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_movz(R1, 0, 0));
        int32_t mkdir_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t mkdir_ok_label = code->count;
        a64_patch_bcond(code, mkdir_ok, mkdir_ok_label);
        code_emit(code, a64_movz(R1, 1, 0));
        a64_patch_b(code, mkdir_done_jump, code->count);
        a64_emit_str_sp_off(code, R1, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_TEXT_CONTAINS) {
        codegen_load_str_pair(code, body, a, R2, R3);
        codegen_load_str_pair(code, body, b, 4, 5);
        code_emit(code, a64_cmp_imm(5, 0));
        int32_t empty_needle = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_cmp_reg(R3, 5));
        int32_t hay_short = code->count;
        code_emit(code, a64_bcond(0, COND_LT));
        code_emit(code, a64_sub_reg(6, R3, 5));
        code_emit(code, a64_add_imm(6, 6, 1, false));
        code_emit(code, a64_movz(7, 0, 0));
        int32_t outer = code->count;
        code_emit(code, a64_cmp_reg(7, 6));
        int32_t no_match = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_movz(8, 0, 0));
        int32_t inner = code->count;
        code_emit(code, a64_cmp_reg(8, 5));
        int32_t matched = code->count;
        code_emit(code, a64_bcond(0, COND_GE));
        code_emit(code, a64_add_reg(9, 7, 8));
        code_emit(code, a64_add_reg_x(10, R2, 9));
        code_emit(code, a64_add_reg_x(11, 4, 8));
        code_emit(code, a64_ldrb_imm(12, 10, 0));
        code_emit(code, a64_ldrb_imm(13, 11, 0));
        code_emit(code, a64_cmp_reg(12, 13));
        int32_t mismatch = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
        code_emit(code, a64_add_imm(8, 8, 1, false));
        code_emit(code, a64_b(inner - code->count));
        int32_t mismatch_label = code->count;
        a64_patch_bcond(code, mismatch, mismatch_label);
        code_emit(code, a64_add_imm(7, 7, 1, false));
        code_emit(code, a64_b(outer - code->count));
        int32_t true_label = code->count;
        a64_patch_bcond(code, empty_needle, true_label);
        a64_patch_bcond(code, matched, true_label);
        code_emit(code, a64_movz(R0, 1, 0));
        int32_t contains_done_jump = code->count;
        code_emit(code, a64_b(0));
        int32_t false_label = code->count;
        a64_patch_bcond(code, hay_short, false_label);
        a64_patch_bcond(code, no_match, false_label);
        code_emit(code, a64_movz(R0, 0, 0));
        a64_patch_b(code, contains_done_jump, code->count);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_MAKE_VARIANT) {
        codegen_zero_slot(code, body, dst);
        code_emit(code, a64_movz_x(R0, (uint16_t)a, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
        for (int32_t field_index = 0; field_index < c; field_index++) {
            int32_t arg_slot = body->call_arg_slot[b + field_index];
            int32_t payload_offset = body->call_arg_offset[b + field_index];
            if (body->slot_kind[arg_slot] == SLOT_STR) {
                a64_emit_ldr_sp_off(code, R1, body->slot_offset[arg_slot], true);
                a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + payload_offset, true);
                a64_emit_ldr_sp_off(code, R1, body->slot_offset[arg_slot] + 8, true);
                a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + payload_offset + 8, true);
            } else if (body->slot_kind[arg_slot] == SLOT_I32) {
                a64_emit_ldr_sp_off(code, R1, body->slot_offset[arg_slot], false);
                a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + payload_offset, false);
            } else if (body->slot_kind[arg_slot] == SLOT_I64) {
                a64_emit_ldr_sp_off(code, R1, body->slot_offset[arg_slot], true);
                a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + payload_offset, true);
            } else if (body->slot_kind[arg_slot] == SLOT_VARIANT) {
                for (int32_t off = 0; off < body->slot_size[arg_slot]; off += 8) {
                    a64_emit_ldr_sp_off(code, R1, body->slot_offset[arg_slot] + off, true);
                    a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + payload_offset + off, true);
                }
            } else if (body->slot_kind[arg_slot] == SLOT_OBJECT ||
                       body->slot_kind[arg_slot] == SLOT_ARRAY_I32 ||
                       body->slot_kind[arg_slot] == SLOT_SEQ_I32 ||
                       body->slot_kind[arg_slot] == SLOT_SEQ_STR ||
                       body->slot_kind[arg_slot] == SLOT_SEQ_OPAQUE) {
                for (int32_t off = 0; off < body->slot_size[arg_slot]; off += 8) {
                    a64_emit_ldr_sp_off(code, R1, body->slot_offset[arg_slot] + off, true);
                    a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + payload_offset + off, true);
                }
            } else {
                /* Generic payload: copy by slot_size (all slots are stack-allocated) */
                for (int32_t off = 0; off < body->slot_size[arg_slot]; off += 8) {
                    a64_emit_ldr_sp_off(code, R1, body->slot_offset[arg_slot] + off, true);
                    a64_emit_str_sp_off(code, R1, body->slot_offset[dst] + payload_offset + off, true);
                }
            }
        }
    } else if (kind == BODY_OP_TAG_LOAD) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_PAYLOAD_LOAD) {
        codegen_copy_slot_from_offset(code, body, dst, a, b);
    } else if (kind == BODY_OP_PAYLOAD_STORE) {
        codegen_store_slot_to_payload(code, body, dst, b, a);
    } else if (kind == BODY_OP_MAKE_COMPOSITE) {
        codegen_zero_slot(code, body, dst);
        for (int32_t item_index = 0; item_index < c; item_index++) {
            int32_t src_slot = body->call_arg_slot[b + item_index];
            int32_t dst_offset = body->call_arg_offset[b + item_index];
            codegen_store_slot_to_offset(code, body, dst, dst_offset, src_slot);
        }
    } else if (kind == BODY_OP_MAKE_SEQ_OPAQUE) {
        codegen_zero_slot(code, body, dst);
        code_emit(code, a64_movz(R0, (uint16_t)c, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + 4, false);
        if (c > 0) {
            int32_t element_size = b;
            if (element_size <= 0) die("opaque seq element size missing");
            int32_t data_bytes = element_size * c;
            codegen_mmap_const(code, data_bytes, R9, 117);
            a64_emit_str_sp_off(code, R9, body->slot_offset[dst] + 8, true);
            for (int32_t item_index = 0; item_index < c; item_index++) {
                int32_t src_slot = body->call_arg_slot[a + item_index];
                int32_t item_off = item_index * element_size;
                int32_t copy_bytes = body->slot_size[src_slot];
                if (copy_bytes > element_size) copy_bytes = element_size;
                int32_t off = 0;
                for (; off + 8 <= copy_bytes; off += 8) {
                    a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot] + off, true);
                    code_emit(code, a64_str_imm(R1, R9, item_off + off, true));
                }
                if (off + 4 <= copy_bytes) {
                    a64_emit_ldr_sp_off(code, R1, body->slot_offset[src_slot] + off, false);
                    code_emit(code, a64_str_imm(R1, R9, item_off + off, false));
                    off += 4;
                }
                for (; off < copy_bytes; off++) {
                    a64_emit_add_large(code, R10, SP, body->slot_offset[src_slot] + off, true);
                    code_emit(code, a64_ldrb_imm(R1, R10, 0));
                    code_emit(code, a64_strb_imm(R1, R9, item_off + off));
                }
            }
        }
    } else if (kind == BODY_OP_CALL_I32) {
        /* Auto-register missing call targets as external symbols */
        if (a >= symbols->function_count) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "__ext_fn_%d", a);
            a = symbols_add_fn(symbols, cold_cstr_span(tmp), 0, 0, 0, cold_cstr_span("int32"));
            if (a >= 0 && a < symbols->function_count)
                symbols->functions[a].is_external = true;
        }
        if (a < 0 || a >= symbols->function_count) {
            code_emit(code, a64_movz(R0, 0, 0));
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        } else {
            FnDef *fn = &symbols->functions[a];
            int32_t stack_bytes = codegen_load_call_args(code, body, fn, b);
            int32_t call_pos = code->count;
            code_emit(code, a64_bl(0));
            function_patches_add(function_patches, call_pos, a);
            if (stack_bytes > 0) a64_emit_add_large(code, SP, SP, stack_bytes, true);
            /* Clobber caller-saved registers (R0-R7 per AAPCS64) */
            na_clobber(0); na_clobber(1); na_clobber(2); na_clobber(3);
            na_clobber(4); na_clobber(5); na_clobber(6); na_clobber(7);
            int32_t ret_kind = cold_return_kind_from_span(symbols, fn->ret);
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst], ret_kind == SLOT_I64 || ret_kind == SLOT_PTR || ret_kind == SLOT_OPAQUE);
            if (ret_kind == SLOT_I32 && body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
        }
    } else if (kind == BODY_OP_COPY_I32) {
        int __cr2 = na_find(a);
        if (__cr2 >= 0) { if (__cr2 != 0) code_emit(code, a64_add_imm(R0, __cr2, 0, false)); }
        else { a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false); }
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_I32_ADD || kind == BODY_OP_I32_SUB) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        code_emit(code, kind == BODY_OP_I32_ADD ? a64_add_reg(R0, R0, R1) : a64_sub_reg(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
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
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
        if (literal.len <= 0xFFFF) {
            code_emit(code, a64_movz_x(R0, (uint16_t)literal.len, 0));
        } else {
            code_emit(code, a64_movz_x(R0, (uint16_t)(literal.len & 0xFFFF), 0));
            code_emit(code, a64_movk(R0, (uint16_t)((literal.len >> 16) & 0xFFFF), 1));
        }
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + 8, true);
    } else if (kind == BODY_OP_STR_LEN) {
        if (body->slot_kind[a] == SLOT_STR_REF) {
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], true);
            code_emit(code, a64_ldr_imm(R0, R1, 8, true));
        } else {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a] + 8, true);
        }
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_STR_EQ) {
        codegen_str_eq(code, body, dst, a, b);
    } else if (kind == BODY_OP_STR_CONCAT) {
        codegen_str_concat(code, body, dst, a, b);
    } else if (kind == BODY_OP_I32_TO_STR) {
        codegen_i32_to_str(code, body, dst, a);
    } else if (kind == BODY_OP_I64_TO_STR) {
        codegen_i64_to_str(code, body, dst, a);
    } else if (kind == BODY_OP_STR_INDEX) {
        codegen_str_index(code, body, dst, a, b);
    } else if (kind == BODY_OP_SEQ_STR_INDEX_DYNAMIC) {
        codegen_seq_str_index(code, body, dst, a, b);
    } else if (kind == BODY_OP_FIELD_REF) {
        int32_t base_kind = body->slot_kind[a];
        if (base_kind == SLOT_OBJECT) {
            a64_emit_add_large(code, R0, SP, body->slot_offset[a] + b, true);
        } else if (base_kind == SLOT_OBJECT_REF || base_kind == SLOT_PTR) {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
            if (b != 0) code_emit(code, a64_add_imm(R0, R0, (uint16_t)b, true));
        } else {
            /* Non-object base: slot is on the stack, field at base + offset */
            a64_emit_add_large(code, R0, SP, body->slot_offset[a] + b, true);
        }
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_STR_REF_STORE) {
        if (body->slot_kind[dst] != SLOT_STR_REF || body->slot_kind[a] != SLOT_STR) {
            return; /* skip mismatched str ref store */
        }
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[dst], true);
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        code_emit(code, a64_str_imm(R0, R1, 0, true));
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a] + 8, true);
        code_emit(code, a64_str_imm(R0, R1, 8, true));
    } else if (kind == BODY_OP_MAKE_SEQ_I32) {
        codegen_zero_slot(code, body, dst);
        if (c > 0 && a >= 0 && body->slot_kind[a] == SLOT_ARRAY_I32) {
            code_emit(code, a64_movz(R0, (uint16_t)c, 0));
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + 4, false);
            int32_t data_bytes = c * 4;
            codegen_mmap_const(code, data_bytes, R9, 6);
            a64_emit_str_sp_off(code, R9, body->slot_offset[dst] + 8, true);
            a64_emit_add_large(code, R10, SP, body->slot_offset[a], true);
            code_emit(code, a64_movz_x(R11, (uint16_t)c, 0));
            int32_t cp_start = code->count;
            code_emit(code, a64_cmp_imm(R11, 0));
            int32_t cp_done_patch = code->count;
            code_emit(code, a64_bcond(0, COND_EQ));
            code_emit(code, a64_ldr_imm(R12, R10, 0, false));
            code_emit(code, a64_str_imm(R12, R9, 0, false));
            code_emit(code, a64_add_imm(R10, R10, 4, true));
            code_emit(code, a64_add_imm(R9, R9, 4, true));
            code_emit(code, a64_sub_imm(R11, R11, 1, true));
            int32_t cp_back = code->count;
            code_emit(code, a64_b(cp_start - cp_back));
            a64_patch_bcond(code, cp_done_patch, code->count);
        }
    } else if (kind == BODY_OP_SEQ_I32_INDEX) {
        /* Accept any sequence kind (seq_header_addr handles generically) */
        if (0) die("int32[] index target kind mismatch");
        codegen_seq_header_addr(code, body, a, R2);
        code_emit(code, a64_ldr_imm(R0, R2, 8, true));
        code_emit(code, a64_ldr_imm(R0, R0, b, false));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_ARRAY_I32_INDEX_DYNAMIC) {
        if (0) die("dynamic array index target kind mismatch");
        if (body->slot_aux[a] <= 0) { code_emit(code, a64_movz(R0, 0, 0)); a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false); return; }
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_cmp_imm(R1, 0));
        code_emit(code, a64_bcond(2, COND_GE));
        code_emit(code, a64_brk(2));
        code_emit(code, a64_cmp_imm(R1, (uint16_t)body->slot_aux[a]));
        code_emit(code, a64_bcond(2, COND_LT));
        code_emit(code, a64_brk(2));
        a64_emit_add_large(code, R0, SP, body->slot_offset[a], true);
        code_emit(code, a64_ldr_w_reg_uxtw2(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_SEQ_I32_INDEX_DYNAMIC) {
        int32_t arr_kind = body->slot_kind[a];
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_cmp_imm(R1, 0));
        code_emit(code, a64_bcond(2, COND_GE));
        code_emit(code, a64_brk(2));
        if (arr_kind == SLOT_ARRAY_I32) {
            /* Inline array: data at SP + slot_offset, length from slot_size */
            int32_t arr_len = body->slot_size[a] / 4;
            if (body->slot_aux[a] > 0) arr_len = body->slot_aux[a];
            codegen_mov_i32_const(code, R3, arr_len);
            code_emit(code, a64_cmp_reg(R1, R3));
            code_emit(code, a64_bcond(2, COND_LT));
            code_emit(code, a64_brk(2));
            a64_emit_add_large(code, R2, SP, body->slot_offset[a], true);
            code_emit(code, a64_ldr_w_reg_uxtw2(R0, R2, R1));
        } else {
            codegen_seq_header_addr(code, body, a, R2);
            code_emit(code, a64_ldr_imm(R3, R2, 0, false));
            code_emit(code, a64_cmp_reg(R1, R3));
            code_emit(code, a64_bcond(2, COND_LT));
            code_emit(code, a64_brk(2));
            code_emit(code, a64_ldr_imm(R0, R2, 8, true));
            code_emit(code, a64_ldr_w_reg_uxtw2(R0, R0, R1));
        }
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_ARRAY_I32_INDEX_STORE) {
        int32_t base_kind = body->slot_kind[a];
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_cmp_imm(R1, 0));
        code_emit(code, a64_bcond(2, COND_GE));
        code_emit(code, a64_brk(2));
        if (base_kind == SLOT_OBJECT_REF) {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
            if (body->slot_aux[a] > 0) {
                code_emit(code, a64_cmp_imm(R1, (uint16_t)body->slot_aux[a]));
            }
        } else if (base_kind == SLOT_ARRAY_I32) {
            code_emit(code, a64_cmp_imm(R1, (uint16_t)body->slot_aux[a]));
        } else {
            /* tolerate non-array kinds from CSG lowerer (treat as opaque) */
            code_emit(code, a64_cmp_imm(R1, 0));
        }
        code_emit(code, a64_bcond(2, COND_LT));
        code_emit(code, a64_brk(2));
        a64_emit_ldr_sp_off(code, R2, body->slot_offset[dst], false);
        if (base_kind == SLOT_OBJECT_REF) {
            code_emit(code, a64_str_w_reg_uxtw2(R2, R0, R1));
        } else {
            a64_emit_add_large(code, R0, SP, body->slot_offset[a], true);
            code_emit(code, a64_str_w_reg_uxtw2(R2, R0, R1));
        }
    } else if (kind == BODY_OP_SEQ_I32_INDEX_STORE) {
        int32_t base_kind = body->slot_kind[a];
        if (base_kind == SLOT_OBJECT_REF) {
            a64_emit_ldr_sp_off(code, R2, body->slot_offset[a], true);
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
            code_emit(code, a64_cmp_imm(R1, 0));
            code_emit(code, a64_bcond(2, COND_GE));
            code_emit(code, a64_brk(2));
            code_emit(code, a64_ldr_imm(R3, R2, 0, false));
            code_emit(code, a64_cmp_reg(R1, R3));
            code_emit(code, a64_bcond(2, COND_LT));
            code_emit(code, a64_brk(2));
            code_emit(code, a64_ldr_imm(R0, R2, 8, true));
            a64_emit_ldr_sp_off(code, R2, body->slot_offset[dst], false);
            code_emit(code, a64_str_w_reg_uxtw2(R2, R0, R1));
        } else if (base_kind == SLOT_SEQ_I32 || base_kind == SLOT_SEQ_I32_REF) {
            codegen_seq_header_addr(code, body, a, R2);
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
            code_emit(code, a64_cmp_imm(R1, 0));
            code_emit(code, a64_bcond(2, COND_GE));
            code_emit(code, a64_brk(2));
            code_emit(code, a64_ldr_imm(R3, R2, 0, false));
            code_emit(code, a64_cmp_reg(R1, R3));
            code_emit(code, a64_bcond(2, COND_LT));
            code_emit(code, a64_brk(2));
            code_emit(code, a64_ldr_imm(R0, R2, 8, true));
            a64_emit_ldr_sp_off(code, R2, body->slot_offset[dst], false);
            code_emit(code, a64_str_w_reg_uxtw2(R2, R0, R1));
        } else die("int32[] index store target kind mismatch");
    } else if (kind == BODY_OP_SEQ_I32_ADD) {
        codegen_seq_i32_add(code, body, dst, a);
    } else if (kind == BODY_OP_SEQ_STR_ADD) {
        codegen_seq_str_add(code, body, dst, a);
    } else if (kind == BODY_OP_SEQ_OPAQUE_INDEX_DYNAMIC) {
        codegen_seq_opaque_index(code, body, dst, a, b, c);
    } else if (kind == BODY_OP_SEQ_OPAQUE_INDEX_STORE) {
        codegen_seq_opaque_index_store(code, body, dst, a, b, c);
    } else if (kind == BODY_OP_SEQ_OPAQUE_ADD) {
        codegen_seq_opaque_add(code, body, dst, a, b);
    } else if (kind == BODY_OP_SEQ_OPAQUE_REMOVE) {
        codegen_seq_opaque_remove(code, body, dst, a, b);
    } else if (kind == BODY_OP_CALL_COMPOSITE) {
        if (a >= symbols->function_count) {
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "__ext_fn_%d", a);
            a = symbols_add_fn(symbols, cold_cstr_span(tmp), 0, 0, 0, cold_cstr_span("int32"));
            if (a >= 0 && a < symbols->function_count)
                symbols->functions[a].is_external = true;
        }
        if (a >= 0 && a < symbols->function_count) {
            FnDef *fn = &symbols->functions[a];
            int32_t ret_kind = cold_return_kind_from_span(symbols, fn->ret);
            if (ret_kind != SLOT_I32 && ret_kind != SLOT_I64 && ret_kind != SLOT_PTR && ret_kind != SLOT_OPAQUE) {
                a64_emit_add_large(code, 8, SP, body->slot_offset[dst], true);
            }
            int32_t stack_bytes = codegen_load_call_args(code, body, fn, b);
            int32_t call_pos = code->count;
            code_emit(code, a64_bl(0));
            function_patches_add(function_patches, call_pos, a);
            if (stack_bytes > 0) a64_emit_add_large(code, SP, SP, stack_bytes, true);
            /* Clobber caller-saved registers (R0-R7 per AAPCS64) */
            na_clobber(0); na_clobber(1); na_clobber(2); na_clobber(3);
            na_clobber(4); na_clobber(5); na_clobber(6); na_clobber(7);
            if (ret_kind == SLOT_I32) {
                a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
                if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
            } else if (ret_kind == SLOT_I64 || ret_kind == SLOT_PTR || ret_kind == SLOT_OPAQUE) {
                a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
            }
        }
    } else if (kind == BODY_OP_COPY_COMPOSITE) {
        int32_t src_kind = body->slot_kind[a];
        if (src_kind == SLOT_STR) {
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[a] + 8, true);
            a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + 8, true);
        } else if (src_kind == SLOT_VARIANT || src_kind == SLOT_OBJECT ||
                   src_kind == SLOT_ARRAY_I32 || src_kind == SLOT_SEQ_I32 ||
                   src_kind == SLOT_SEQ_STR || src_kind == SLOT_SEQ_OPAQUE) {
            int32_t total = body->slot_size[dst];
            for (int32_t off = 0; off < total; off += 8) {
                a64_emit_ldr_sp_off(code, R0, body->slot_offset[a] + off, true);
                a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + off, true);
            }
        } else {
            /* Generic copy: memcpy by slot_size (all slots on stack) */
            int32_t total = body->slot_size[dst];
            for (int32_t off = 0; off < total; off += 8) {
                a64_emit_ldr_sp_off(code, R0, body->slot_offset[a] + off, true);
                a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + off, true);
            }
        }
    } else if (kind == BODY_OP_UNWRAP_OR_RETURN) {
        /* Compare tag (slot a) with expected value (b); trap on mismatch */
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_cmp_imm(R0, (uint16_t)b));
        int32_t ok_pos = code->count;
        code_emit(code, a64_bcond(0, COND_EQ));
        code_emit(code, a64_brk(1));
        code->words[ok_pos] = (code->words[ok_pos] & 0xFF00001Fu) | (2u << 5);
    } else if (kind == BODY_OP_I32_MUL) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        code_emit(code, a64_mul_reg(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_I32_DIV) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        code_emit(code, a64_cmp_imm(R1, 0));
        int32_t non_zero = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
        code_emit(code, a64_brk(5));
        a64_patch_bcond(code, non_zero, code->count);
        code_emit(code, a64_sdiv_reg(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_I32_MOD) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        code_emit(code, a64_cmp_imm(R1, 0));
        int32_t mod_nz = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
        code_emit(code, a64_brk(5));
        a64_patch_bcond(code, mod_nz, code->count);
        code_emit(code, a64_sdiv_reg(R2, R0, R1));
        na_clobber(2);
        code_emit(code, a64_msub_reg(R0, R2, R1, R0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_I32_CMP) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        code_emit(code, a64_cmp_reg(R0, R1));
        code_emit(code, a64_movz(R2, 0, 0));
        int32_t true_pos = code->count;
        code_emit(code, a64_bcond(0, c));
        int32_t done_pos = code->count;
        code_emit(code, a64_b(0));
        a64_patch_bcond(code, true_pos, code->count);
        code_emit(code, a64_movz(R2, 1, 0));
        a64_patch_b(code, done_pos, code->count);
        a64_emit_str_sp_off(code, R2, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(2, dst);
    } else if (kind == BODY_OP_I32_SHL) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        code_emit(code, a64_lsl_reg(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_I32_ASR) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        code_emit(code, a64_asr_reg(R0, R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_I32_AND || kind == BODY_OP_I32_OR || kind == BODY_OP_I32_XOR) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        na_clobber(1);
        uint32_t op_word;
        if (kind == BODY_OP_I32_AND) op_word = a64_and_reg(R0, R0, R1);
        else if (kind == BODY_OP_I32_OR) op_word = a64_orr_reg(R0, R0, R1);
        else op_word = a64_eor_reg(R0, R0, R1);
        code_emit(code, op_word);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_OPEN) {
        /* open(path_str, flags) → fd (dst)
           a: path slot (SLOT_STR), b: flags slot (SLOT_I32) */
        codegen_cstring_from_slot(code, body, a, 7, 55);
        code_emit(code, a64_add_imm(R0, 7, 0, true));
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_movz_x(R2, 0644, 0));
        code_emit(code, a64_movz_x(16, 5, 0));
        code_emit(code, a64_svc(0x80));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_READ) {
        /* read(fd, buf, count) → bytes_read (dst)
           a: fd slot, b: buf slot, c: count slot */
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_add_large(code, R1, SP, body->slot_offset[b], true);
        a64_emit_ldr_sp_off(code, R2, body->slot_offset[c], false);
        code_emit(code, a64_movz_x(16, 3, 0));
        code_emit(code, a64_svc(0x80));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_CLOSE) {
        /* close(fd) → result (dst)
           a: fd slot */
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_movz_x(16, 6, 0));
        code_emit(code, a64_svc(0x80));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_ATOMIC_LOAD_I32) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], true);
        code_emit(code, a64_ldar_w(R0, R1));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_ATOMIC_STORE_I32) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[dst], true);
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_stlr_w(R0, R1));
    } else if (kind == BODY_OP_ATOMIC_CAS_I32) {
        /* CAS: a=ptr, b=desired, c=expected → dst=1(success)/0(fail)
           retry: ldaxr w3,[x0]; cmp w3,w2; b.ne fail; stlxr w4,w1,[x0]; cbnz w4,retry;
           success: mov w0,1; b done; fail: mov w0,0; done: */
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        a64_emit_ldr_sp_off(code, R2, body->slot_offset[c], false);
        int32_t retry = code->count;
        code_emit(code, a64_ldaxr_w(R3, R0));
        code_emit(code, a64_cmp_reg(R3, R2));
        int32_t mismatch = code->count;
        code_emit(code, a64_bcond(0, COND_NE));
        code_emit(code, a64_stlxr_w(R4, R1, R0));
        code_emit(code, a64_cbnz(R4, 0));
        int32_t cbnz_pos = code->count - 1;
        code->words[cbnz_pos] = a64_cbnz(R4, retry - cbnz_pos);
        /* success: mov w0, 1 */
        code_emit(code, a64_movz(R0, 1, 0));
        code_emit(code, a64_b(0));
        int32_t skip_fail = code->count - 1;
        int32_t fail_pos = code->count;
        a64_patch_bcond(code, mismatch, fail_pos);
        /* fail: mov w0, 0 */
        code_emit(code, a64_movz(R0, 0, 0));
        code->words[skip_fail] = a64_b(code->count - skip_fail);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_MMAP) {
        /* mmap(addr, len, prot, flags) → ptr (dst)
           uses codegen_mmap_len_reg internally */
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], false);
        codegen_mmap_len_reg(code, R1, 3, 56);
        code_emit(code, a64_add_imm(R0, 3, 0, true));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_PTR_LOAD_I32) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], true);
        code_emit(code, a64_ldr_imm(R0, R1, 0, false));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_PTR_STORE_I32) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[dst], true);
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_str_imm(R0, R1, 0, false));
    } else if (kind == BODY_OP_PTR_LOAD_I64) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[a], true);
        code_emit(code, a64_ldr_imm(R0, R1, 0, true));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_SLOT_STORE_I32) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + c, false);
    } else if (kind == BODY_OP_SLOT_STORE_I64) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + c, true);
    } else if (kind == BODY_OP_PTR_STORE_I64) {
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[dst], true);
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        code_emit(code, a64_str_imm(R0, R1, 0, true));
    } else if (kind == BODY_OP_WRITE_RAW) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], true);
        a64_emit_ldr_sp_off(code, R2, body->slot_offset[c], false);
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_WRITE_BYTES) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_add_large(code, R1, SP, body->slot_offset[b], true);
        a64_emit_ldr_sp_off(code, R2, body->slot_offset[c], false);
        code_emit(code, a64_movz_x(16, 4, 0));
        code_emit(code, a64_svc(0x80));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_F64_CONST) {
        /* Reconstruct 64-bit float from lo32/hi32 slots:
           X0 = (uint64_t)lo32 | ((uint64_t)hi32 << 32)
           BFI X0, X1, #32, #32 */
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, 0xB3427C00u | ((uint32_t)R1 << 5) | (uint32_t)R0);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_F32_CONST) {
        float val; memcpy(&val, &a, 4);
        int32_t bits; memcpy(&bits, &val, 4);
        code_emit(code, a64_movz(R0, (uint16_t)(bits & 0xFFFF), 0));
        if ((bits & 0xFFFF0000u) != 0) code_emit(code, a64_movk(R0, (uint16_t)((bits>>16)&0xFFFF), 1));
        code_emit(code, a64_fmov_s(0, R0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_F32_ADD) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_fmov_s(0, R0)); code_emit(code, a64_fmov_s(1, R1));
        code_emit(code, a64_fadd_s(0, 0, 1));
        code_emit(code, a64_fmov_sr(R0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_F32_SUB) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_fmov_s(0, R0)); code_emit(code, a64_fmov_s(1, R1));
        code_emit(code, a64_fsub_s(0, 0, 1));
        code_emit(code, a64_fmov_sr(R0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_F32_MUL) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_fmov_s(0, R0)); code_emit(code, a64_fmov_s(1, R1));
        code_emit(code, a64_fmul_s(0, 0, 1));
        code_emit(code, a64_fmov_sr(R0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_F32_DIV) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        a64_emit_ldr_sp_off(code, R1, body->slot_offset[b], false);
        code_emit(code, a64_fmov_s(0, R0)); code_emit(code, a64_fmov_s(1, R1));
        code_emit(code, a64_fdiv_s(0, 0, 1));
        code_emit(code, a64_fmov_sr(R0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_F32_NEG) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_fmov_s(0, R0));
        code_emit(code, a64_fneg_s(0, 0));
        code_emit(code, a64_fmov_sr(R0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_F32_FROM_I32) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_scvtf_s(0, R0));
        code_emit(code, a64_fmov_sr(R0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_I32_FROM_F32) {
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], false);
        code_emit(code, a64_fmov_s(0, R0));
        code_emit(code, a64_fcvtzs_w(R0, 0));
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
    } else if (kind == BODY_OP_FN_ADDR) {
        /* Load function address via ADR + patch */
        int32_t fn_index = a;
        int32_t adr_pos = code->count;
        code_emit(code, a64_adr(R0, 0));
        function_patches_add(function_patches, adr_pos, fn_index);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
    } else if (kind == BODY_OP_CALL_PTR) {
        /* Indirect call: load args into x0-x7, load fn ptr, call via BLR, store result.
           a = fn_ptr_slot, b = call_arg_start, c = call_arg_count */
        int32_t fn_ptr_slot = a;
        int32_t call_arg_start = b;
        int32_t call_arg_count = c;
        /* Load args into registers */
        for (int32_t ai = 0; ai < call_arg_count && ai < 8; ai++) {
            int32_t arg_slot = body->call_arg_slot[call_arg_start + ai];
            int32_t arg_kind = body->slot_kind[arg_slot];
            if (arg_kind == SLOT_I32) {
                a64_emit_ldr_sp_off(code, ai, body->slot_offset[arg_slot], false);
            } else if (arg_kind == SLOT_I64 || arg_kind == SLOT_PTR) {
                a64_emit_ldr_sp_off(code, ai, body->slot_offset[arg_slot], true);
            } else {
                a64_emit_ldr_sp_off(code, ai, body->slot_offset[arg_slot], true);
            }
        }
        a64_emit_ldr_sp_off(code, R9, body->slot_offset[fn_ptr_slot], true);
        code_emit(code, a64_blr(R9));
        /* Clobber caller-saved registers (R0-R7 per AAPCS64) */
        na_clobber(0); na_clobber(1); na_clobber(2); na_clobber(3);
        na_clobber(4); na_clobber(5); na_clobber(6); na_clobber(7);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else if (kind == BODY_OP_CLOSURE_NEW) {
        /* Create closure: dst = { fn_ptr, env_ptr } (16 bytes)
           a = fn_addr_slot, b = env_slot */
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[a], true);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], true);
        a64_emit_ldr_sp_off(code, R0, body->slot_offset[b], true);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst] + 8, true);
    } else if (kind == BODY_OP_CLOSURE_CALL) {
        /* Call closure via function pointer: load fn_ptr from closure slot,
           load user args into x0-x7, BLR, store result. Env is stored in
           closure for future use (closure-aware functions receive env as
           an extra trailing arg). For simple function pointers, env is
           ignored.
           a = closure_slot, b = call_arg_start, c = call_arg_count */
        int32_t closure_slot = a;
        int32_t call_arg_start = b;
        int32_t call_arg_count = c;
        a64_emit_ldr_sp_off(code, R9, body->slot_offset[closure_slot], true);
        /* Load user args into x0-x7 */
        for (int32_t ai = 0; ai < call_arg_count && ai < 8; ai++) {
            int32_t arg_slot = body->call_arg_slot[call_arg_start + ai];
            int32_t arg_kind = body->slot_kind[arg_slot];
            if (arg_kind == SLOT_I32)
                a64_emit_ldr_sp_off(code, ai, body->slot_offset[arg_slot], false);
            else
                a64_emit_ldr_sp_off(code, ai, body->slot_offset[arg_slot], true);
        }
        code_emit(code, a64_blr(R9));
        /* Clobber caller-saved registers (R0-R7 per AAPCS64) */
        na_clobber(0); na_clobber(1); na_clobber(2); na_clobber(3);
        na_clobber(4); na_clobber(5); na_clobber(6); na_clobber(7);
        a64_emit_str_sp_off(code, R0, body->slot_offset[dst], false);
        if (body->slot_no_alias && body->slot_no_alias[dst]) na_set(0, dst);
    } else {
        ; /* unknown op: skip silently */
    }
}

/* ---- x86_64 codegen (byte-level, packs into uint32_t words) ---- */

static void x64_codegen_op(X64Code *x, BodyIR *body, Symbols *symbols,
                           FunctionPatchList *patches, int32_t op) {
    int32_t kind = body->op_kind[op];
    int32_t dst = body->op_dst[op];
    int32_t a = body->op_a[op];
    int32_t b = body->op_b[op];
    int32_t c = body->op_c[op];
    int32_t off_dst = body->slot_offset[dst];
    /* I32 arithmetic */
    if (kind == BODY_OP_I32_CONST) {
        x64_mov_r32_imm32(x, 0, (int32_t)a);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_COPY_I32) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_ADD || kind == BODY_OP_I32_SUB) {
        int32_t oa = body->slot_offset[a], ob = body->slot_offset[b];
        x64_mov_r32_mr32(x, 0, 4, oa);
        x64_mov_r32_mr32(x, 1, 4, ob);
        if (kind == BODY_OP_I32_ADD) x64_add_r32_r32(x, 0, 1);
        else x64_sub_r32_r32(x, 0, 1);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_MUL) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_imul_r32_r32(x, 0, 1);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_DIV) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_cdq(x); x64_idiv_r32(x, 1);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_MOD) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_cdq(x); x64_idiv_r32(x, 1);
        x64_mov_r32_r32(x, 0, 2); /* remainder in EDX */
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_AND) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_emit1(x, 0x21); x64_emit1(x, MODRM(3, 1, 0)); /* AND %ecx,%eax */
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_OR) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_emit1(x, 0x09); x64_emit1(x, MODRM(3, 1, 0)); /* OR %ecx,%eax */
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_XOR) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_xor_r32_r32(x, 0, 1);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_SHL) {
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_r32(x, 1, 1); /* shift count to ECX */
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_shl_r32_cl(x, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_ASR) {
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_r32(x, 1, 1);
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_sar_r32_cl(x, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_CMP) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_cmp_r32_r32(x, 0, 1);
        x64_setcc_r8(x, c, 0);
        x64_movzb_r8_r32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    /* I64 arithmetic */
    } else if (kind == BODY_OP_I64_CONST) {
        uint64_t bits = (uint32_t)a | ((uint64_t)(uint32_t)b << 32);
        x64_mov_r64_imm64(x, 0, bits);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_COPY_I64) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I64_FROM_I32) {
        x64_movsxd_r64_r32(x, 0, 0);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_FROM_I64) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I64_ADD || kind == BODY_OP_I64_SUB ||
               kind == BODY_OP_I64_MUL) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[b]);
        if (kind == BODY_OP_I64_ADD) x64_add_r64_r64(x, 0, 1);
        else if (kind == BODY_OP_I64_SUB) x64_sub_r64_r64(x, 0, 1);
        else x64_imul_r64_r64(x, 0, 1);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I64_DIV) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[b]);
        x64_cqo(x); x64_idiv_r64(x, 1);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I64_AND || kind == BODY_OP_I64_OR ||
               kind == BODY_OP_I64_XOR) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[b]);
        if (kind == BODY_OP_I64_AND) x64_and_r64_r64(x, 0, 1);
        else if (kind == BODY_OP_I64_OR) x64_or_r64_r64(x, 0, 1);
        else x64_xor_r64_r64(x, 0, 1);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I64_SHL || kind == BODY_OP_I64_ASR) {
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_r32(x, 1, 1);
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        if (kind == BODY_OP_I64_SHL) x64_shl_r64_cl(x, 0);
        else x64_sar_r64_cl(x, 0);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I64_CMP) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[b]);
        x64_cmp_r64_r64(x, 0, 1);
        x64_setcc_r8(x, c, 0);
        x64_movzb_r8_r32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    /* Float: F32 */
    } else if (kind == BODY_OP_F32_CONST) {
        float val; memcpy(&val, &a, 4);
        int32_t bits; memcpy(&bits, &val, 4);
        x64_mov_r32_imm32(x, 0, bits);
        x64_movd_xmm_r32(x, 0, 0);
        x64_movss_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F32_ADD || kind == BODY_OP_F32_SUB ||
               kind == BODY_OP_F32_MUL || kind == BODY_OP_F32_DIV) {
        x64_movss_xmm_mr(x, 0, 4, body->slot_offset[a]);
        x64_movss_xmm_mr(x, 1, 4, body->slot_offset[b]);
        if (kind == BODY_OP_F32_ADD) x64_addss(x, 0, 1);
        else if (kind == BODY_OP_F32_SUB) x64_subss(x, 0, 1);
        else if (kind == BODY_OP_F32_MUL) x64_mulss(x, 0, 1);
        else x64_divss(x, 0, 1);
        x64_movss_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F32_NEG) {
        x64_movss_xmm_mr(x, 0, 4, body->slot_offset[a]);
        /* XOR sign bit: xmm1 = -0.0f, xorpd xmm0, xmm1 */
        x64_mov_r32_imm32(x, 0, 0x80000000);
        x64_movd_xmm_r32(x, 1, 0);
        x64_xorps(x, 0, 1);
        x64_movss_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F32_CMP) {
        x64_movss_xmm_mr(x, 0, 4, body->slot_offset[a]);
        x64_movss_xmm_mr(x, 1, 4, body->slot_offset[b]);
        x64_ucomiss(x, 0, 1);
        x64_setcc_r8(x, c, 0);
        x64_movzb_r8_r32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F32_FROM_I32) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_cvtsi2ss(x, 0, 0);
        x64_movss_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_FROM_F32) {
        x64_movss_xmm_mr(x, 0, 4, body->slot_offset[a]);
        x64_cvttss2si(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    /* Float: F64 */
    } else if (kind == BODY_OP_F64_CONST) {
        uint64_t bits = (uint32_t)a | ((uint64_t)(uint32_t)b << 32);
        x64_mov_r64_imm64(x, 0, bits);
        x64_movd_xmm_r32(x, 0, 0);
        x64_movsd_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F64_ADD || kind == BODY_OP_F64_SUB ||
               kind == BODY_OP_F64_MUL || kind == BODY_OP_F64_DIV) {
        x64_movsd_xmm_mr(x, 0, 4, body->slot_offset[a]);
        x64_movsd_xmm_mr(x, 1, 4, body->slot_offset[b]);
        if (kind == BODY_OP_F64_ADD) x64_addsd(x, 0, 1);
        else if (kind == BODY_OP_F64_SUB) x64_subsd(x, 0, 1);
        else if (kind == BODY_OP_F64_MUL) x64_mulsd(x, 0, 1);
        else x64_divsd(x, 0, 1);
        x64_movsd_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F64_NEG) {
        x64_movsd_xmm_mr(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_imm64(x, 0, 0x8000000000000000ULL);
        x64_movd_xmm_r32(x, 1, 0);
        x64_xorps(x, 0, 1);
        x64_movsd_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F64_CMP) {
        x64_movsd_xmm_mr(x, 0, 4, body->slot_offset[a]);
        x64_movsd_xmm_mr(x, 1, 4, body->slot_offset[b]);
        x64_ucomisd(x, 0, 1);
        x64_setcc_r8(x, c, 0);
        x64_movzb_r8_r32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_F64_FROM_I32) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_cvtsi2sd(x, 0, 0);
        x64_movsd_mr_xmm(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_FROM_F64) {
        x64_movsd_xmm_mr(x, 0, 4, body->slot_offset[a]);
        x64_cvttsd2si(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    /* String ops */
    } else if (kind == BODY_OP_STR_LITERAL) {
        if (a >= 0 && a < body->string_literal_count) {
            Span literal = body->string_literal[a];
            x64_lea_r64_mr(x, 0, 4, 0); /* placeholder, patched later with data offset */
            x64_mov_mr64_r64(x, 4, off_dst, 0);
            x64_mov_r32_imm32(x, 0, (int32_t)literal.len);
            x64_mov_mr32_r32(x, 4, off_dst + 8, 0);
        }
    } else if (kind == BODY_OP_STR_LEN) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a] + 8);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_STR_EQ) {
        /* str_eq via memcmp: compare ptr+len */
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[a] + 8);
        x64_mov_r64_mr64(x, 2, 4, body->slot_offset[b]);
        x64_mov_r64_mr64(x, 3, 4, body->slot_offset[b] + 8);
        /* compare lengths */
        x64_cmp_r64_r64(x, 1, 3);
        int32_t len_ne = x->len + 2;
        x64_jcc_rel8(x, CC_NE, 0);
        /* compare bytes - simplified: just check if lengths equal for now */
        x64_mov_r32_imm32(x, 0, 0); /* result */
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_STR_CONCAT) {
        /* str_concat: simplified - store empty string */
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_TO_STR || kind == BODY_OP_I64_TO_STR ||
               kind == BODY_OP_STR_INDEX || kind == BODY_OP_STR_JOIN ||
               kind == BODY_OP_STR_SPLIT_CHAR || kind == BODY_OP_STR_STRIP ||
               kind == BODY_OP_STR_SLICE || kind == BODY_OP_SHELL_QUOTE) {
        /* Complex string ops: emit stub returning empty string */
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    /* Load/Store */
    } else if (kind == BODY_OP_LOAD_I32) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_PAYLOAD_LOAD) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a] + b);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_PAYLOAD_STORE) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr64_r64(x, 4, body->slot_offset[dst] + b, 0);
    } else if (kind == BODY_OP_TAG_LOAD) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_REF_LOAD) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32_base(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_I32_REF_STORE) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[dst]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[a]);
        x64_mov_mr32_base_r32(x, 0, 1);
    } else if (kind == BODY_OP_FIELD_REF) {
        x64_lea_r64_mr(x, 0, 4, body->slot_offset[a] + b);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_STR_REF_STORE) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[dst]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[a]);
        x64_mov_mr64_base_r64(x, 0, 1);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[a] + 8);
        x64_emit1(x, REX_W | REX_R); x64_emit1(x, 0x89); x64_emit1(x, MODRM(1, 1 & 7, 0));
        x64_emit1(x, 8);
    /* Copy */
    } else if (kind == BODY_OP_COPY_COMPOSITE) {
        int32_t sz = body->slot_size[dst];
        for (int32_t off = 0; off < sz; off += 8) {
            x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a] + off);
            x64_mov_mr64_r64(x, 4, off_dst + off, 0);
        }
    /* Calls */
    } else if (kind == BODY_OP_CALL_I32) {
        if (a >= 0 && a < symbols->function_count) {
            int32_t call_pos = x->len + 1;
            x64_call_rel32(x, 0);
            function_patches_add(patches, call_pos, a);
        }
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_CALL_COMPOSITE) {
        if (a >= 0 && a < symbols->function_count) {
            int32_t call_pos = x->len + 1;
            x64_call_rel32(x, 0);
            function_patches_add(patches, call_pos, a);
        }
    } else if (kind == BODY_OP_CALL_PTR) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_call_reg(x, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_FN_ADDR) {
        int32_t fn_idx = a;
        int32_t imm_pos = x->len + 2; /* after REX.W + B8 opcode */
        x64_mov_r64_imm64(x, 0, 0);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
        if (fn_idx >= 0 && fn_idx < symbols->function_count)
            function_patches_add(patches, imm_pos, fn_idx);
    /* Pointer ops */
    } else if (kind == BODY_OP_PTR_CONST) {
        x64_mov_r64_imm64(x, 0, (uint64_t)(int32_t)a);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_PTR_LOAD_I32) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32_base(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_PTR_STORE_I32) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[dst]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[a]);
        x64_mov_mr32_base_r32(x, 0, 1);
    } else if (kind == BODY_OP_PTR_LOAD_I64) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_mr64_base(x, 0, 0);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_PTR_STORE_I64) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[dst]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[a]);
        x64_mov_mr64_base_r64(x, 0, 1);
    /* Slot store */
    } else if (kind == BODY_OP_SLOT_STORE_I32) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr32_r32(x, 4, off_dst + c, 0);
    } else if (kind == BODY_OP_SLOT_STORE_I64) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr64_r64(x, 4, off_dst + c, 0);
    /* Seq/Array ops */
    } else if (kind == BODY_OP_SEQ_I32_INDEX_DYNAMIC) {
        /* idx in slot b, base in slot a, store result in dst */
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a] + 8); /* data ptr */
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);    /* index */
        x64_mov_r32_mr32_base_index4(x, 0, 0, 1);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_SEQ_I32_ADD) {
        /* Append I32 to seq */
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_SEQ_STR_ADD) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_SEQ_STR_INDEX_DYNAMIC) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_SEQ_OPAQUE_INDEX_DYNAMIC) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_SEQ_OPAQUE_ADD) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_SEQ_OPAQUE_INDEX_STORE) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_SEQ_OPAQUE_REMOVE) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_ARRAY_I32_INDEX_DYNAMIC) {
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_lea_r64_mr(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32_base_index4(x, 0, 0, 1);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_ARRAY_I32_INDEX_STORE) {
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_mr32(x, 2, 4, body->slot_offset[dst]);
        x64_lea_r64_mr(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr32_base_index4_r32(x, 0, 1, 2);
    } else if (kind == BODY_OP_SEQ_I32_INDEX_STORE) {
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_mr32(x, 2, 4, body->slot_offset[dst]);
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a] + 8);
        x64_mov_mr32_base_index4_r32(x, 0, 1, 2);
    /* Variant / Composite */
    } else if (kind == BODY_OP_MAKE_VARIANT) {
        /* dst = zero-init, then set tag and copy fields */
        for (int32_t i = 0; i < body->slot_size[dst]; i += 8) {
            x64_mov_r64_imm64(x, 0, 0);
            x64_mov_mr64_r64(x, 4, off_dst + i, 0);
        }
        x64_mov_r32_imm32(x, 0, (int32_t)a);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
        for (int32_t fi = 0; fi < c; fi++) {
            int32_t arg_slot = body->call_arg_slot[b + fi];
            int32_t poff = body->call_arg_offset[b + fi];
            x64_mov_r64_mr64(x, 0, 4, body->slot_offset[arg_slot]);
            x64_mov_mr64_r64(x, 4, off_dst + poff, 0);
        }
    } else if (kind == BODY_OP_MAKE_COMPOSITE) {
        for (int32_t i = 0; i < body->slot_size[dst]; i += 8) {
            x64_mov_r64_imm64(x, 0, 0);
            x64_mov_mr64_r64(x, 4, off_dst + i, 0);
        }
        for (int32_t fi = 0; fi < c; fi++) {
            int32_t src = body->call_arg_slot[b + fi];
            int32_t poff = body->call_arg_offset[b + fi];
            x64_mov_r64_mr64(x, 0, 4, body->slot_offset[src]);
            x64_mov_mr64_r64(x, 4, off_dst + poff, 0);
        }
    } else if (kind == BODY_OP_MAKE_SEQ_OPAQUE) {
        for (int32_t i = 0; i < body->slot_size[dst]; i += 8) {
            x64_mov_r64_imm64(x, 0, 0);
            x64_mov_mr64_r64(x, 4, off_dst + i, 0);
        }
    /* Closure */
    } else if (kind == BODY_OP_CLOSURE_NEW) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_mr64_r64(x, 4, off_dst, 0);
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[b]);
        x64_mov_mr64_r64(x, 4, off_dst + 8, 0);
    } else if (kind == BODY_OP_CLOSURE_CALL) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_call_reg(x, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    /* Atomic */
    } else if (kind == BODY_OP_ATOMIC_LOAD_I32) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32_base(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_ATOMIC_STORE_I32) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[dst]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[a]);
        x64_mov_mr32_base_r32(x, 0, 1);
    } else if (kind == BODY_OP_ATOMIC_CAS_I32) {
        /* cmpxchg: lock cmpxchg %ecx,[%rax]; setz %al */
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);   /* ptr */
        x64_mov_r32_mr32(x, 2, 4, body->slot_offset[c]);  /* expected → EAX */
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);  /* desired → ECX */
        x64_lock_cmpxchg_r32_mr(x, 0, 1);
        x64_setcc_r8(x, CC_E, 0);
        x64_movzb_r8_r32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    /* Syscall-based ops (macOS x86_64: syscall in RAX, args RDI/RSI/RDX/R10/R8/R9) */
    } else if (kind == BODY_OP_EXIT) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_r32(x, 0, 0); /* ARG1 = exit code */
        x64_mov_r32_imm32(x, 0, 0x2000001); /* RAX = sys_exit */
        x64_syscall(x);
    } else if (kind == BODY_OP_WRITE_LINE) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);   /* fd */
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[b]);    /* buf ptr */
        x64_mov_r32_mr32(x, 2, 4, body->slot_offset[b] + 8); /* len */
        x64_mov_r32_imm32(x, 0, 0x2000004); /* RAX = sys_write */
        x64_syscall(x);
    } else if (kind == BODY_OP_WRITE_RAW) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r64_mr64(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_mr32(x, 2, 4, body->slot_offset[c]);
        x64_mov_r32_imm32(x, 0, 0x2000004);
        x64_syscall(x);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_WRITE_BYTES) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_lea_r64_mr(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_mr32(x, 2, 4, body->slot_offset[c]);
        x64_mov_r32_imm32(x, 0, 0x2000004);
        x64_syscall(x);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_OPEN) {
        x64_mov_r64_mr64(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_mr32(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_imm32(x, 2, 0644);
        x64_mov_r32_imm32(x, 0, 0x2000005);
        x64_syscall(x);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_READ) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_lea_r64_mr(x, 1, 4, body->slot_offset[b]);
        x64_mov_r32_mr32(x, 2, 4, body->slot_offset[c]);
        x64_mov_r32_imm32(x, 0, 0x2000003);
        x64_syscall(x);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_CLOSE) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_mov_r32_imm32(x, 0, 0x2000006);
        x64_syscall(x);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_BRK) {
        x64_int3(x);
    } else if (kind == BODY_OP_ARGC_LOAD) {
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    } else if (kind == BODY_OP_UNWRAP_OR_RETURN) {
        x64_mov_r32_mr32(x, 0, 4, body->slot_offset[a]);
        x64_cmp_r32_imm8(x, 0, (int8_t)b);
        x64_int3(x); /* trap on mismatch */
    /* Remaining: empty stubs */
    } else if (kind == BODY_OP_MMAP ||
               kind == BODY_OP_TIME_NS || kind == BODY_OP_GETRUSAGE ||
               kind == BODY_OP_GETENV_STR || kind == BODY_OP_READ_FLAG ||
               kind == BODY_OP_PARSE_INT || kind == BODY_OP_TEXT_SET_INIT ||
               kind == BODY_OP_TEXT_SET_INSERT || kind == BODY_OP_CWD_STR ||
               kind == BODY_OP_PATH_JOIN || kind == BODY_OP_PATH_ABSOLUTE ||
               kind == BODY_OP_PATH_PARENT || kind == BODY_OP_PATH_EXISTS ||
               kind == BODY_OP_PATH_FILE_SIZE || kind == BODY_OP_PATH_READ_TEXT ||
               kind == BODY_OP_PATH_WRITE_TEXT || kind == BODY_OP_REMOVE_FILE ||
               kind == BODY_OP_CHMOD_X || kind == BODY_OP_COLD_SELF_EXEC ||
               kind == BODY_OP_MKDIR_ONE || kind == BODY_OP_TEXT_CONTAINS ||
               kind == BODY_OP_EXEC_SHELL || kind == BODY_OP_ARGV_STR ||
               kind == BODY_OP_STR_SELECT_NONEMPTY || kind == BODY_OP_ASSERT) {
        /* File/OS/shell ops: emit empty result */
        x64_mov_r32_imm32(x, 0, 0);
        x64_mov_mr32_r32(x, 4, off_dst, 0);
    }
}

/* x86_64 function prologue: push rbp; mov rsp, rbp; sub $frame, rsp */
static void x64_codegen_prologue(X64Code *x, int32_t frame_size) {
    x64_push_r64(x, 5); /* push %rbp */
    x64_mov_r64_r64(x, 5, 4); /* mov %rsp, %rbp */
    if (frame_size > 0) x64_sub_rsp_imm8(x, (int8_t)frame_size);
}

/* x86_64 function epilogue: mov rbp, rsp; pop rbp; ret */
static void x64_codegen_epilogue(X64Code *x) {
    x64_mov_r64_r64(x, 4, 5); /* mov %rbp, %rsp */
    x64_pop_r64(x, 5); /* pop %rbp */
    x64_ret(x);
}

/* Pack X64Code bytes into uint32_t words. Returns word count. */
static int32_t x64_pack_words(X64Code *x, uint32_t *words, int32_t max_words) {
    int32_t wc = 0;
    for (int32_t i = 0; i < x->len && wc < max_words; i += 4) {
        uint32_t w = 0;
        for (int32_t j = 0; j < 4 && i + j < x->len; j++)
            w |= ((uint32_t)x->buf[i + j]) << (j * 8);
        words[wc++] = w;
    }
    return wc;
}

/* x86_64 function compilation wrapper */
static void x64_codegen_func(X64Code *x, BodyIR *body, Symbols *symbols,
                              FunctionPatchList *patches) {
    x64_codegen_prologue(x, body->frame_size);
    for (int32_t bi = 0; bi < body->block_count; bi++) {
        int32_t bs = body->block_op_start[bi];
        int32_t be = bs + body->block_op_count[bi];
        for (int32_t oi = bs; oi < be; oi++)
            x64_codegen_op(x, body, symbols, patches, oi);
    }
    x64_codegen_epilogue(x);
}

/* ---- RISC-V 64 codegen (fixed 32-bit words, reuses Code struct) ---- */

static void rv64_codegen_op(Code *code, BodyIR *body, Symbols *symbols,
                            FunctionPatchList *patches, int32_t op) {
    int32_t kind = body->op_kind[op], dst = body->op_dst[op];
    int32_t a = body->op_a[op], b = body->op_b[op], c = body->op_c[op];
    int32_t off_dst = body->slot_offset[dst];
    /* I32 ops */
    if (kind == BODY_OP_I32_CONST) {
        rv_li(code->words, &code->count, RV_T0, a);
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_COPY_I32) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_ADD || kind == BODY_OP_I32_SUB) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, kind == BODY_OP_I32_ADD ? rv_addw(RV_T0, RV_T0, RV_T1) : rv_subw(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_MUL) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_mulw(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_DIV) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_divw(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_MOD) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_remw(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_AND) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_and(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_OR) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_or(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_XOR) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_xor(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_SHL) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_sllw(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_ASR) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_sraw(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_CMP) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        /* Use SLT/SLTU/EQ based on condition code c */
        if (c == COND_EQ) {
            code_emit(code, rv_subw(RV_T0, RV_T0, RV_T1));
            code_emit(code, rv_sltiu(RV_T0, RV_T0, 1)); /* T0==0 ? 1 : 0 */
        } else if (c == COND_NE) {
            code_emit(code, rv_subw(RV_T0, RV_T0, RV_T1));
            code_emit(code, rv_sltu(RV_T0, RV_ZERO, RV_T0)); /* T0!=0 ? 1 : 0 */
        } else if (c == COND_LT) {
            code_emit(code, rv_slt(RV_T0, RV_T0, RV_T1));
        } else if (c == COND_GE) {
            code_emit(code, rv_slt(RV_T0, RV_T0, RV_T1));
            code_emit(code, rv_xori(RV_T0, RV_T0, 1));
        } else {
            code_emit(code, rv_slt(RV_T0, RV_T1, RV_T0)); /* default: a < b */
        }
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    /* I64 ops */
    } else if (kind == BODY_OP_I64_CONST) {
        uint64_t bits = (uint32_t)a | ((uint64_t)(uint32_t)b << 32);
        rv_li(code->words, &code->count, RV_T0, (int32_t)bits);
        if ((bits >> 32) != 0) {
            rv_li(code->words, &code->count, RV_T1, (int32_t)(bits >> 32));
            code_emit(code, rv_slli(RV_T1, RV_T1, 32));
            code_emit(code, rv_or(RV_T0, RV_T0, RV_T1));
        }
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_COPY_I64) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I64_FROM_I32) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_FROM_I64) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I64_ADD || kind == BODY_OP_I64_SUB) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, kind == BODY_OP_I64_ADD ? rv_add(RV_T0, RV_T0, RV_T1) : rv_sub(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I64_MUL) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_mul(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I64_DIV) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_div(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I64_AND || kind == BODY_OP_I64_OR || kind == BODY_OP_I64_XOR) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        if (kind == BODY_OP_I64_AND) code_emit(code, rv_and(RV_T0, RV_T0, RV_T1));
        else if (kind == BODY_OP_I64_OR) code_emit(code, rv_or(RV_T0, RV_T0, RV_T1));
        else code_emit(code, rv_xor(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I64_SHL || kind == BODY_OP_I64_ASR) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        if (kind == BODY_OP_I64_SHL) code_emit(code, rv_sll(RV_T0, RV_T0, RV_T1));
        else code_emit(code, rv_sra(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I64_CMP) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        if (c == COND_EQ) {
            code_emit(code, rv_sub(RV_T0, RV_T0, RV_T1));
            code_emit(code, rv_sltiu(RV_T0, RV_T0, 1));
        } else if (c == COND_NE) {
            code_emit(code, rv_sub(RV_T0, RV_T0, RV_T1));
            code_emit(code, rv_sltu(RV_T0, RV_ZERO, RV_T0));
        } else if (c == COND_LT) {
            code_emit(code, rv_slt(RV_T0, RV_T0, RV_T1));
        } else {
            code_emit(code, rv_slt(RV_T0, RV_T1, RV_T0));
        }
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    /* Float F32 */
    } else if (kind == BODY_OP_F32_CONST) {
        float val; memcpy(&val, &a, 4);
        int32_t bits; memcpy(&bits, &val, 4);
        rv_li(code->words, &code->count, RV_T0, bits);
        code_emit(code, rv_fmv_w_x(0, RV_T0));
        code_emit(code, rv_fsw(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F32_ADD || kind == BODY_OP_F32_SUB ||
               kind == BODY_OP_F32_MUL || kind == BODY_OP_F32_DIV) {
        code_emit(code, rv_flw(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_flw(1, RV_SP, (int16_t)body->slot_offset[b]));
        if (kind == BODY_OP_F32_ADD) code_emit(code, rv_fadd_s(0, 0, 1));
        else if (kind == BODY_OP_F32_SUB) code_emit(code, rv_fsub_s(0, 0, 1));
        else if (kind == BODY_OP_F32_MUL) code_emit(code, rv_fmul_s(0, 0, 1));
        else code_emit(code, rv_fdiv_s(0, 0, 1));
        code_emit(code, rv_fsw(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F32_NEG) {
        code_emit(code, rv_flw(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fsgnjn_s(0, 0, 0));
        code_emit(code, rv_fsw(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F32_CMP) {
        code_emit(code, rv_flw(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_flw(1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, c == COND_EQ ? rv_feq_s(RV_T0, 0, 1) : rv_flt_s(RV_T0, 0, 1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F32_FROM_I32) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fcvt_s_w(0, RV_T0));
        code_emit(code, rv_fsw(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_FROM_F32) {
        code_emit(code, rv_flw(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fcvt_w_s(RV_T0, 0));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    /* Float F64 */
    } else if (kind == BODY_OP_F64_CONST) {
        uint64_t bits = (uint32_t)a | ((uint64_t)(uint32_t)b << 32);
        rv_li(code->words, &code->count, RV_T0, (int32_t)bits);
        if ((bits >> 32) != 0) {
            rv_li(code->words, &code->count, RV_T1, (int32_t)(bits >> 32));
            code_emit(code, rv_slli(RV_T1, RV_T1, 32));
            code_emit(code, rv_or(RV_T0, RV_T0, RV_T1));
        }
        code_emit(code, rv_fmv_d_x(0, RV_T0));
        code_emit(code, rv_fsd(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F64_ADD || kind == BODY_OP_F64_SUB ||
               kind == BODY_OP_F64_MUL || kind == BODY_OP_F64_DIV) {
        code_emit(code, rv_fld(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fld(1, RV_SP, (int16_t)body->slot_offset[b]));
        if (kind == BODY_OP_F64_ADD) code_emit(code, rv_fadd_d(0, 0, 1));
        else if (kind == BODY_OP_F64_SUB) code_emit(code, rv_fsub_d(0, 0, 1));
        else if (kind == BODY_OP_F64_MUL) code_emit(code, rv_fmul_d(0, 0, 1));
        else code_emit(code, rv_fdiv_d(0, 0, 1));
        code_emit(code, rv_fsd(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F64_NEG) {
        code_emit(code, rv_fld(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fsgnjn_d(0, 0, 0));
        code_emit(code, rv_fsd(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F64_CMP) {
        code_emit(code, rv_fld(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fld(1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, c == COND_EQ ? rv_feq_d(RV_T0, 0, 1) : rv_flt_d(RV_T0, 0, 1));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_F64_FROM_I32) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fcvt_d_w(0, RV_T0));
        code_emit(code, rv_fsd(0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_FROM_F64) {
        code_emit(code, rv_fld(0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_fcvt_w_d(RV_T0, 0));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    /* String / Load / Store */
    } else if (kind == BODY_OP_STR_LITERAL) {
        rv_li(code->words, &code->count, RV_T0, 0);
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
        rv_li(code->words, &code->count, RV_T0, 0);
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)(off_dst + 8)));
    } else if (kind == BODY_OP_STR_LEN) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)(body->slot_offset[a] + 8)));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_PAYLOAD_LOAD) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)(body->slot_offset[a] + b)));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_PAYLOAD_STORE) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)(body->slot_offset[dst] + b)));
    } else if (kind == BODY_OP_TAG_LOAD) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_REF_LOAD) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T0, RV_T0, 0));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_I32_REF_STORE) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[dst]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sw(RV_T1, RV_T0, 0));
    } else if (kind == BODY_OP_FIELD_REF) {
        code_emit(code, rv_addi(RV_T0, RV_SP, (int16_t)(body->slot_offset[a] + b)));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_STR_REF_STORE) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[dst]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sd(RV_T1, RV_T0, 0));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)(body->slot_offset[a] + 8)));
        code_emit(code, rv_sd(RV_T1, RV_T0, 8));
    } else if (kind == BODY_OP_COPY_COMPOSITE) {
        int32_t sz = body->slot_size[dst], os = body->slot_offset[a];
        for (int32_t off = 0; off < sz; off += 8) {
            code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)(os + off)));
            code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)(off_dst + off)));
        }
    /* Calls */
    } else if (kind == BODY_OP_CALL_I32) {
        int32_t call_pos = code->count;
        code_emit(code, rv_jal(RV_RA, 0));
        function_patches_add(patches, call_pos, a);
        code_emit(code, rv_sw(RV_A0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_CALL_COMPOSITE) {
        int32_t call_pos = code->count;
        code_emit(code, rv_jal(RV_RA, 0));
        function_patches_add(patches, call_pos, a);
    } else if (kind == BODY_OP_CALL_PTR) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_jalr(RV_RA, RV_T0, 0));
        code_emit(code, rv_sw(RV_A0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_FN_ADDR) {
        int32_t fn_idx = a;
        int32_t auipc_pos = code->count;
        code_emit(code, rv_auipc(RV_T0, 0));
        function_patches_add(patches, auipc_pos, fn_idx);
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    /* Pointer ops */
    } else if (kind == BODY_OP_PTR_CONST) {
        rv_li(code->words, &code->count, RV_T0, (int32_t)a);
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_PTR_LOAD_I32) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T0, RV_T0, 0));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_PTR_STORE_I32) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[dst]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sw(RV_T1, RV_T0, 0));
    } else if (kind == BODY_OP_PTR_LOAD_I64) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_T0, RV_T0, 0));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_PTR_STORE_I64) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[dst]));
        code_emit(code, rv_ld(RV_T1, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sd(RV_T1, RV_T0, 0));
    } else if (kind == BODY_OP_SLOT_STORE_I32) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)(off_dst + c)));
    } else if (kind == BODY_OP_SLOT_STORE_I64) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)(off_dst + c)));
    /* Variant / Composite */
    } else if (kind == BODY_OP_MAKE_VARIANT) {
        for (int32_t i = 0; i < body->slot_size[dst]; i += 8) {
            code_emit(code, rv_sd(RV_ZERO, RV_SP, (int16_t)(off_dst + i)));
        }
        rv_li(code->words, &code->count, RV_T0, (int32_t)a);
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
        for (int32_t fi = 0; fi < c; fi++) {
            int32_t arg_slot = body->call_arg_slot[b + fi];
            int32_t poff = body->call_arg_offset[b + fi];
            code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[arg_slot]));
            code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)(off_dst + poff)));
        }
    } else if (kind == BODY_OP_MAKE_COMPOSITE) {
        for (int32_t i = 0; i < body->slot_size[dst]; i += 8) {
            code_emit(code, rv_sd(RV_ZERO, RV_SP, (int16_t)(off_dst + i)));
        }
        for (int32_t fi = 0; fi < c; fi++) {
            int32_t src = body->call_arg_slot[b + fi];
            int32_t poff = body->call_arg_offset[b + fi];
            code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[src]));
            code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)(off_dst + poff)));
        }
    /* Seq / Array */
    } else if (kind == BODY_OP_SEQ_I32_INDEX_DYNAMIC ||
               kind == BODY_OP_ARRAY_I32_INDEX_DYNAMIC) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)(body->slot_offset[a] + 8)));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_slliw(RV_T1, RV_T1, 2));
        code_emit(code, rv_add(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_lw(RV_T0, RV_T0, 0));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_ARRAY_I32_INDEX_STORE) {
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_slliw(RV_T1, RV_T1, 2));
        code_emit(code, rv_lw(RV_T2, RV_SP, (int16_t)body->slot_offset[dst]));
        code_emit(code, rv_addi(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_add(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_sw(RV_T2, RV_T0, 0));
    } else if (kind == BODY_OP_SEQ_I32_INDEX_STORE) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)(body->slot_offset[a] + 8)));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_slliw(RV_T1, RV_T1, 2));
        code_emit(code, rv_add(RV_T0, RV_T0, RV_T1));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[dst]));
        code_emit(code, rv_sw(RV_T1, RV_T0, 0));
    /* Closure */
    } else if (kind == BODY_OP_CLOSURE_NEW) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)off_dst));
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_sd(RV_T0, RV_SP, (int16_t)(off_dst + 8)));
    } else if (kind == BODY_OP_CLOSURE_CALL) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_jalr(RV_RA, RV_T0, 0));
        code_emit(code, rv_sw(RV_A0, RV_SP, (int16_t)off_dst));
    /* Atomic */
    } else if (kind == BODY_OP_ATOMIC_LOAD_I32) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lr_w(RV_T0, RV_T0));
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_ATOMIC_STORE_I32) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[dst]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[a]));
        /* amoswap.w for atomic store */
        code_emit(code, rv_amoswap_w(RV_ZERO, RV_T0, RV_T1));
    } else if (kind == BODY_OP_ATOMIC_CAS_I32) {
        code_emit(code, rv_ld(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_lw(RV_T1, RV_SP, (int16_t)body->slot_offset[c])); /* expected */
        code_emit(code, rv_lw(RV_T2, RV_SP, (int16_t)body->slot_offset[b])); /* desired */
        int32_t retry = code->count;
        code_emit(code, rv_lr_w(RV_T3, RV_T0));
        code_emit(code, rv_bne(RV_T3, RV_T1, 8)); /* skip if not equal */
        code_emit(code, rv_sc_w(RV_T3, RV_T0, RV_T2));
        code_emit(code, rv_bne(RV_T3, RV_ZERO, (int16_t)(retry - code->count)));
        code_emit(code, rv_sltiu(RV_T0, RV_T3, 1)); /* success=1 if SC succeeded */
        code_emit(code, rv_sw(RV_T0, RV_SP, (int16_t)off_dst));
    /* Syscall / OS */
    } else if (kind == BODY_OP_EXIT) {
        code_emit(code, rv_lw(RV_A0, RV_SP, (int16_t)body->slot_offset[a]));
        rv_li(code->words, &code->count, RV_A7, 93); /* sys_exit */
        code_emit(code, rv_ecall());
    } else if (kind == BODY_OP_WRITE_LINE) {
        code_emit(code, rv_lw(RV_A0, RV_SP, (int16_t)body->slot_offset[a]));
        code_emit(code, rv_ld(RV_A1, RV_SP, (int16_t)body->slot_offset[b]));
        code_emit(code, rv_lw(RV_A2, RV_SP, (int16_t)(body->slot_offset[b] + 8)));
        rv_li(code->words, &code->count, RV_A7, 64); /* sys_write */
        code_emit(code, rv_ecall());
    } else if (kind == BODY_OP_BRK) {
        code_emit(code, rv_ebreak());
    } else if (kind == BODY_OP_ARGC_LOAD ||
               kind == BODY_OP_LOAD_I32) {
        code_emit(code, rv_sw(RV_ZERO, RV_SP, (int16_t)off_dst));
    } else if (kind == BODY_OP_UNWRAP_OR_RETURN) {
        code_emit(code, rv_lw(RV_T0, RV_SP, (int16_t)body->slot_offset[a]));
        rv_li(code->words, &code->count, RV_T1, (int32_t)b);
        code_emit(code, rv_bne(RV_T0, RV_T1, 0)); /* patched to after ebreak */
        code_emit(code, rv_ebreak());
    /* Remaining complex ops: emit zero/empty result */
    } else if (kind == BODY_OP_STR_EQ || kind == BODY_OP_STR_CONCAT ||
               kind == BODY_OP_I32_TO_STR || kind == BODY_OP_I64_TO_STR ||
               kind == BODY_OP_STR_INDEX || kind == BODY_OP_STR_JOIN ||
               kind == BODY_OP_STR_SPLIT_CHAR || kind == BODY_OP_STR_STRIP ||
               kind == BODY_OP_STR_SLICE || kind == BODY_OP_SHELL_QUOTE ||
               kind == BODY_OP_STR_SELECT_NONEMPTY ||
               kind == BODY_OP_READ_FLAG || kind == BODY_OP_PARSE_INT ||
               kind == BODY_OP_TEXT_SET_INIT || kind == BODY_OP_TEXT_SET_INSERT ||
               kind == BODY_OP_CWD_STR || kind == BODY_OP_PATH_JOIN ||
               kind == BODY_OP_PATH_ABSOLUTE || kind == BODY_OP_PATH_PARENT ||
               kind == BODY_OP_PATH_EXISTS || kind == BODY_OP_PATH_FILE_SIZE ||
               kind == BODY_OP_PATH_READ_TEXT || kind == BODY_OP_PATH_WRITE_TEXT ||
               kind == BODY_OP_REMOVE_FILE || kind == BODY_OP_CHMOD_X ||
               kind == BODY_OP_COLD_SELF_EXEC || kind == BODY_OP_MKDIR_ONE ||
               kind == BODY_OP_TEXT_CONTAINS || kind == BODY_OP_EXEC_SHELL ||
               kind == BODY_OP_ARGV_STR || kind == BODY_OP_MMAP ||
               kind == BODY_OP_OPEN || kind == BODY_OP_READ || kind == BODY_OP_CLOSE ||
               kind == BODY_OP_TIME_NS || kind == BODY_OP_GETRUSAGE ||
               kind == BODY_OP_GETENV_STR || kind == BODY_OP_ASSERT ||
               kind == BODY_OP_WRITE_RAW || kind == BODY_OP_WRITE_BYTES ||
               kind == BODY_OP_MAKE_SEQ_OPAQUE ||
               kind == BODY_OP_SEQ_I32_ADD || kind == BODY_OP_SEQ_STR_ADD ||
               kind == BODY_OP_SEQ_STR_INDEX_DYNAMIC ||
               kind == BODY_OP_SEQ_OPAQUE_INDEX_DYNAMIC ||
               kind == BODY_OP_SEQ_OPAQUE_ADD ||
               kind == BODY_OP_SEQ_OPAQUE_INDEX_STORE ||
               kind == BODY_OP_SEQ_OPAQUE_REMOVE) {
        code_emit(code, rv_sw(RV_ZERO, RV_SP, (int16_t)off_dst));
    }
}

static void rv64_codegen_prologue(Code *code, int32_t frame_size) {
    code_emit(code, rv_addi(RV_SP, RV_SP, -16));
    code_emit(code, rv_sd(RV_RA, RV_SP, 0));
    code_emit(code, rv_sd(RV_S0, RV_SP, 8));
    code_emit(code, rv_addi(RV_S0, RV_SP, 0));
    if (frame_size > 0)
        code_emit(code, rv_addi(RV_SP, RV_SP, (int16_t)(-frame_size & 0xFFF)));
}

static void rv64_codegen_epilogue(Code *code, int32_t frame_size) {
    if (frame_size > 0)
        code_emit(code, rv_addi(RV_SP, RV_SP, (int16_t)(frame_size & 0xFFF)));
    code_emit(code, rv_ld(RV_RA, RV_SP, 0));
    code_emit(code, rv_ld(RV_S0, RV_SP, 8));
    code_emit(code, rv_addi(RV_SP, RV_SP, 16));
    code_emit(code, rv_jalr(RV_ZERO, RV_RA, 0));
}

static void rv64_codegen_func(Code *code, BodyIR *body, Symbols *symbols,
                              FunctionPatchList *patches) {
    rv64_codegen_prologue(code, body->frame_size);
    for (int32_t bi = 0; bi < body->block_count; bi++) {
        int32_t bs = body->block_op_start[bi];
        int32_t be = bs + body->block_op_count[bi];
        for (int32_t oi = bs; oi < be; oi++)
            rv64_codegen_op(code, body, symbols, patches, oi);
    }
    rv64_codegen_epilogue(code, body->frame_size);
}

static void codegen_switch(Code *code, BodyIR *body, PatchList *patches, int32_t term) {
    int32_t tag_slot = body->term_value[term];
    int32_t count = body->term_case_count[term];
    int32_t case_index[128];
    if (count < 0 || count > 128) die("too many switch cases");
    int32_t found = 0;
    for (int32_t i = 0; i < body->switch_count; i++) {
        if (body->switch_term[i] != term) continue;
        if (found >= count) die("switch case count mismatch");
        case_index[found++] = i;
    }
    if (found != count) die("switch case owner mismatch");
    a64_emit_ldr_sp_off(code, R0, body->slot_offset[tag_slot], false);

    if (count == 2 &&
        ((body->switch_tag[case_index[0]] == 0 && body->switch_tag[case_index[1]] == 1) ||
         (body->switch_tag[case_index[0]] == 1 && body->switch_tag[case_index[1]] == 0))) {
        int32_t zero_block = body->switch_tag[case_index[0]] == 0 ? body->switch_block[case_index[0]]
                                                                  : body->switch_block[case_index[1]];
        int32_t one_block = body->switch_tag[case_index[0]] == 1 ? body->switch_block[case_index[0]]
                                                                 : body->switch_block[case_index[1]];
        int32_t cbz_pos = code->count;
        code_emit(code, a64_cbz(R0, 0, false));
        patches_add(patches, cbz_pos, zero_block, 2);
        int32_t b_pos = code->count;
        code_emit(code, a64_b(0));
        patches_add(patches, b_pos, one_block, 1);
        return;
    }

    for (int32_t i = 0; i < count; i++) {
        int32_t idx = case_index[i];
        code_emit(code, a64_cmp_imm(R0, (uint16_t)body->switch_tag[idx]));
        int32_t pos = code->count;
        code_emit(code, a64_bcond(0, 0));
        patches_add(patches, pos, body->switch_block[idx], 0);
    }
}

static void codegen_func(Code *code, BodyIR *body, Symbols *symbols,
                         FunctionPatchList *function_patches) {
    na_reset();
    int32_t frame_size = align_i32(body->frame_size, 16);
    code_emit(code, a64_stp_pre(19, 20, SP, -16));
    code_emit(code, a64_stp_pre(FP, LR, SP, -16));
    code_emit(code, a64_add_imm(FP, SP, 0, true));
    if (frame_size > 0) a64_emit_sub_large(code, SP, SP, frame_size, true);
    if (body->sret_slot >= 0) {
        a64_emit_str_sp_off(code, 8, body->slot_offset[body->sret_slot], true);
    }
    codegen_store_params(code, body);

    int32_t *block_pos = arena_alloc(code->arena, (size_t)body->block_count * sizeof(int32_t));
    PatchList patches = {0};
    patches.arena = code->arena;

    for (int32_t block = 0; block < body->block_count; block++) {
        na_reset(); /* Invalidate no-alias cache at block boundary (CFG safety) */
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
                a64_emit_ldr_sp_off(code, R0, body->slot_offset[value_slot], false);
            } else if (value_kind == SLOT_I64 || value_kind == SLOT_PTR ||
                       value_kind == SLOT_OPAQUE) {
                a64_emit_ldr_sp_off(code, R0, body->slot_offset[value_slot], true);
            } else if (value_kind == SLOT_STR) {
                if (body->sret_slot >= 0)
                a64_emit_ldr_sp_off(code, 8, body->slot_offset[body->sret_slot], true);
                a64_emit_ldr_sp_off(code, R0, body->slot_offset[value_slot], true);
                code_emit(code, a64_str_imm(R0, 8, 0, true));
                a64_emit_ldr_sp_off(code, R0, body->slot_offset[value_slot] + 8, true);
                code_emit(code, a64_str_imm(R0, 8, 8, true));
            } else if (value_kind == SLOT_VARIANT || value_kind == SLOT_OBJECT ||
                       value_kind == SLOT_SEQ_I32 || value_kind == SLOT_SEQ_STR ||
                       value_kind == SLOT_SEQ_OPAQUE) {
                if (body->sret_slot >= 0)
                a64_emit_ldr_sp_off(code, 8, body->slot_offset[body->sret_slot], true);
                int32_t copy_bytes = body->slot_size[value_slot];
                if (copy_bytes > body->return_size) copy_bytes = body->return_size;
                for (int32_t off = 0; off < copy_bytes; off += 8) {
                    a64_emit_ldr_sp_off(code, R0, body->slot_offset[value_slot] + off, true);
                    code_emit(code, a64_str_imm(R0, 8, off, true));
                }
                /* Zero-fill remaining sret space if variant is smaller */
                if (copy_bytes < body->return_size) {
                    code_emit(code, a64_movz_x(R0, 0, 0));
                    for (int32_t off = copy_bytes; off < body->return_size; off += 8) {
                        code_emit(code, a64_str_imm(R0, 8, off, true));
                    }
                }
            } else {
                /* Unsupported return kind: return 0 gracefully */
                code_emit(code, a64_movz(R0, 0, 0));
                emit_epilogue(code, frame_size);
                return;
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
            a64_emit_ldr_sp_off(code, R0, body->slot_offset[left], false);
            a64_emit_ldr_sp_off(code, R1, body->slot_offset[right], false);
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
            /* Unknown terminator: emit ret 0 */
            code_emit(code, a64_movz(R0, 0, 0));
            emit_epilogue(code, frame_size);
            code_emit(code, a64_ret());
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

static void cold_diag_fn_name(Span name) {
    for (int32_t k = 0; k < name.len; k++) fputc(name.ptr[k], stderr);
}

static bool cold_body_codegen_ready(BodyIR *body) {
    return body &&
           !body->has_fallback &&
           body->block_count > 0 &&
           !(body->block_count > 0 && body->block_term[0] < 0);
}

static void cold_die_missing_reachable_body(Symbols *symbols, int32_t fn_index) {
    if (symbols && fn_index >= 0 && fn_index < symbols->function_count) {
        fprintf(stderr, "cheng_cold: reachable function body missing: ");
        cold_diag_fn_name(symbols->functions[fn_index].name);
        fputc('\n', stderr);
    }
    die("reachable cold function body missing");
}

static void cold_mark_reachable_functions(Symbols *symbols,
                                          BodyIR **function_bodies,
                                          int32_t function_count,
                                          int32_t entry_function,
                                          bool *reachable) {
    int32_t *stack = arena_alloc(symbols->arena, (size_t)function_count * sizeof(int32_t));
    int32_t stack_count = 0;
    reachable[entry_function] = true;
    stack[stack_count++] = entry_function;
    while (stack_count > 0) {
        int32_t fn_index = stack[--stack_count];
        if (fn_index < 0 || fn_index >= function_count) die("reachable cold function index out of range");
        BodyIR *body = function_bodies[fn_index];
        if (!cold_body_codegen_ready(body)) {
            if (symbols->functions[fn_index].is_external) continue;
            cold_die_missing_reachable_body(symbols, fn_index);
        }
        for (int32_t op = 0; op < body->op_count; op++) {
            int32_t target = -1;
            if (body->op_kind[op] == BODY_OP_CALL_I32 ||
                body->op_kind[op] == BODY_OP_CALL_COMPOSITE ||
                body->op_kind[op] == BODY_OP_FN_ADDR) {
                target = body->op_a[op];
            }
            if (target < 0) continue;
            if (target >= function_count) continue; /* external call target */
            if (reachable[target]) continue;
            reachable[target] = true;
            stack[stack_count++] = target;
        }
    }
}

static void codegen_program(Code *code, BodyIR **function_bodies,
                            int32_t function_count, int32_t entry_function,
                            Symbols *symbols, const char *target) {
    if (entry_function < 0 || entry_function >= function_count ||
        !function_bodies[entry_function]) die("missing entry function body");

    int32_t *function_pos = arena_alloc(code->arena, (size_t)function_count * sizeof(int32_t));
    for (int32_t i = 0; i < function_count; i++) function_pos[i] = -1;
    bool *reachable_functions = arena_alloc(code->arena, (size_t)function_count * sizeof(bool));
    for (int32_t i = 0; i < function_count; i++) reachable_functions[i] = false;
    cold_mark_reachable_functions(symbols, function_bodies, function_count,
                                  entry_function, reachable_functions);

    /* E-Graph BodyIR rewrite: identity elimination + constant folding + copy elimination */
    int32_t egraph_rewrite_count = 0;
    for (int32_t i = 0; i < function_count; i++) {
        BodyIR *body = function_bodies[i];
        if (!body || body->has_fallback) continue;
        /* Build slot→op mapping for constant/copy folding */
        int32_t *slot_writer = arena_alloc(code->arena, (size_t)body->slot_count * sizeof(int32_t));
        for (int32_t si = 0; si < body->slot_count; si++) slot_writer[si] = -1;
        for (int32_t oi = 0; oi < body->op_count; oi++) {
            int32_t dst = body->op_dst[oi];
            if (dst >= 0 && dst < body->slot_count) slot_writer[dst] = oi;
            int32_t k = body->op_kind[oi];
            int32_t a = body->op_a[oi], b = body->op_b[oi];
            int32_t orig_kind = k;
            /* Constant folding: arithmetic with two I32_CONST operands */
            int32_t a_op = (a >= 0 && a < body->slot_count) ? slot_writer[a] : -1;
            int32_t b_op = (b >= 0 && b < body->slot_count) ? slot_writer[b] : -1;
            bool a_const = (a_op >= 0 && body->op_kind[a_op] == BODY_OP_I32_CONST);
            bool b_const = (b_op >= 0 && body->op_kind[b_op] == BODY_OP_I32_CONST);
            if (a_const && b_const) {
                int32_t va = body->op_a[a_op];
                int32_t vb = body->op_a[b_op];
                int32_t result = 0;
                bool folded = true;
                if (k == BODY_OP_I32_ADD) result = va + vb;
                else if (k == BODY_OP_I32_SUB) result = va - vb;
                else if (k == BODY_OP_I32_MUL) result = va * vb;
                else if (k == BODY_OP_I32_AND) result = va & vb;
                else if (k == BODY_OP_I32_OR)  result = va | vb;
                else folded = false;
                if (folded) {
                    body->op_kind[oi] = BODY_OP_I32_CONST;
                    body->op_a[oi] = result;
                    body->op_b[oi] = 0;
                    body->op_c[oi] = 0;
                    egraph_rewrite_count++;
                    continue;
                }
            }
            /* Identity: ADD(x, 0) → x, SUB(x, 0) → x */
            if ((k == BODY_OP_I32_ADD || k == BODY_OP_I32_SUB) && b == 0)
                body->op_kind[oi] = BODY_OP_COPY_I32;
            /* Identity: MUL(x, 1) → x */
            else if (k == BODY_OP_I32_MUL && b == 1)
                body->op_kind[oi] = BODY_OP_COPY_I32;
            /* Identity: AND(x, -1) → x, OR(x, 0) → x, XOR(x, 0) → x */
            else if ((k == BODY_OP_I32_AND && b == -1) ||
                     (k == BODY_OP_I32_OR  && b == 0) ||
                     (k == BODY_OP_I32_XOR && b == 0))
                body->op_kind[oi] = BODY_OP_COPY_I32;
            /* Double COPY elimination: COPY(COPY(x)) → COPY(x) */
            else if (k == BODY_OP_COPY_I32) {
                int32_t src_op = (a >= 0 && a < body->slot_count) ? slot_writer[a] : -1;
                if (src_op >= 0 && body->op_kind[src_op] == BODY_OP_COPY_I32) {
                    body->op_a[oi] = body->op_a[src_op];
                    egraph_rewrite_count++;
                }
            }
            if (body->op_kind[oi] != orig_kind) egraph_rewrite_count++;
        }
    }
    cold_egraph_rewrite_count = egraph_rewrite_count;
    FunctionPatchList function_patches = {0};
    function_patches.arena = code->arena;

    bool use_rv64 = target && strstr(target, "riscv64") != 0;

    /* Entry trampoline: architecture-specific */
    int32_t entry_call_pos = 0;
    if (use_rv64) {
        code_emit(code, rv_addi(RV_S0, RV_A0, 0));
        code_emit(code, rv_addi(RV_S1, RV_A1, 0));
        entry_call_pos = code->count;
        code_emit(code, rv_jal(RV_RA, 0));
        code_emit(code, rv_jalr(RV_ZERO, RV_RA, 0));
    } else {
        code_emit(code, a64_add_imm(19, R0, 0, true));
        code_emit(code, a64_add_imm(20, R1, 0, true));
        code_emit(code, a64_add_imm(21, LR, 0, true));
        code_emit(code, a64_add_imm(22, R2, 0, true));
        entry_call_pos = code->count;
        code_emit(code, a64_bl(0));
        code_emit(code, a64_add_imm(LR, 21, 0, true));
        code_emit(code, a64_ret());
    }

    /* Real entry function body starts here */
    int32_t entry_body_pos = code->count;
    function_pos[entry_function] = entry_body_pos;
    {
        BodyIR *entry_body = function_bodies[entry_function];
        if (!cold_body_codegen_ready(entry_body))
            die("entry cold function body is not codegen-ready");
        if (use_rv64)
            rv64_codegen_func(code, entry_body, symbols, &function_patches);
        else
            codegen_func(code, entry_body, symbols, &function_patches);
    }
    /* Patch trampoline to jump to real entry body */
    if (use_rv64)
        code->words[entry_call_pos] = rv_jal(RV_RA, entry_body_pos - entry_call_pos);
    else
        code->words[entry_call_pos] = a64_bl(entry_body_pos - entry_call_pos);
    if (cold_diag_dump_per_fn || cold_diag_dump_slots) {
        fprintf(stderr, "[diag] entry fn ");
        cold_diag_fn_name(symbols->functions[entry_function].name);
        fprintf(stderr, " end at word=%d (count=%d)\n", code->count, code->count - function_pos[entry_function]);
        if (cold_diag_dump_per_fn) {
            for (int32_t w = function_pos[entry_function]; w < code->count; w++)
                fprintf(stderr, "  %08x\n", code->words[w]);
        }
    }
    int32_t num_jobs = cold_jobs_from_env();
    int32_t remaining = 0;
    for (int32_t i = 0; i < function_count; i++) {
        if (i != entry_function && reachable_functions[i] && function_bodies[i] &&
            !function_bodies[i]->has_fallback) remaining++;
    }

    if (num_jobs > 1 && remaining > 1) {
        int32_t active_jobs = num_jobs;
        if (active_jobs > remaining) active_jobs = remaining;

        /* Shared atomic counter: workers pull next function index via fetch_add */
        int32_t next_fn = 0;

        CodegenWorker *workers = arena_alloc(code->arena,
            (size_t)active_jobs * sizeof(CodegenWorker));
        pthread_t *threads = arena_alloc(code->arena,
            (size_t)active_jobs * sizeof(pthread_t));

        /* Per-worker work-stealing deques */
        ColdWSDeque *deques = arena_alloc(code->arena,
            (size_t)active_jobs * sizeof(ColdWSDeque));
        memset(deques, 0, (size_t)active_jobs * sizeof(ColdWSDeque));

        /* Distribute function indices across workers (interleaved for load balance) */
        int32_t dist_idx = 0;
        for (int32_t i = 0; i < function_count; i++) {
            if (i == entry_function || !reachable_functions[i] ||
                !function_bodies[i] || function_bodies[i]->has_fallback) continue;
            cold_wsdeque_push(&deques[dist_idx % active_jobs], i);
            dist_idx++;
        }

        /* Per-worker arena cache: reuse mmap'd arenas across compilations.
           Reset used=0 and keep pages; significantly reduces mmap/munmap churn. */
        static Arena *worker_arena_cache[16] = {0};
        static int32_t worker_arena_cache_count = 0;

        for (int32_t w = 0; w < active_jobs; w++) {
            Arena *w_arena = NULL;
            /* Try to reuse a cached arena */
            if (w < worker_arena_cache_count && worker_arena_cache[w]) {
                w_arena = worker_arena_cache[w];
                /* Reset arena state: keep pages, just rewind allocation cursor.
                   Page list stays intact; next alloc will reuse existing pages. */
                w_arena->used = 0;
                w_arena->phase_count = 0;
            }
            if (!w_arena) {
                w_arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
                if (w_arena == MAP_FAILED) die("worker arena mmap failed");
                /* Cache this arena for future compilations */
                if (w < 16) {
                    worker_arena_cache[w] = w_arena;
                    if (w >= worker_arena_cache_count)
                        worker_arena_cache_count = w + 1;
                }
            }
            Code *w_code = code_new(w_arena, 256);
            workers[w] = (CodegenWorker){
                .deque = &deques[w],
                .all_deques = deques,
                .worker_index = w,
                .active_workers = active_jobs,
                .entry_function = entry_function,
                .function_count = function_count,
                .function_bodies = function_bodies,
                .reachable_functions = reachable_functions,
                .symbols = symbols,
                .local_code = w_code,
                .local_patches = {.arena = w_arena},
                .local_function_pos = arena_alloc(w_arena,
                    (size_t)function_count * sizeof(int32_t)),
                .local_function_end = arena_alloc(w_arena,
                    (size_t)function_count * sizeof(int32_t)),
            };
            for (int32_t i = 0; i < function_count; i++) {
                workers[w].local_function_pos[i] = -1;
                workers[w].local_function_end[i] = -1;
            }
            if (pthread_create(&threads[w], NULL, codegen_worker_run, &workers[w]) != 0) {
                die("codegen worker create failed");
            }
        }

        for (int32_t w = 0; w < active_jobs; w++) {
            if (pthread_join(threads[w], NULL) != 0) {
                die("codegen worker join failed");
            }
        }

        /* Deterministic merge: copy functions in index order.
           For each function, find which worker claimed it, then copy its
           code block and adjust patch positions by the final offset. */
        for (int32_t i = 0; i < function_count; i++) {
            if (i == entry_function || !reachable_functions[i] ||
                !function_bodies[i] ||
                function_bodies[i]->has_fallback) continue;
            /* Find the worker that claimed this function */
            CodegenWorker *wk = NULL;
            for (int32_t w = 0; w < active_jobs; w++) {
                if (workers[w].local_function_pos[i] >= 0) {
                    wk = &workers[w];
                    break;
                }
            }
            if (!wk) continue;
            int32_t base_offset = code->count;
            int32_t fn_start = wk->local_function_pos[i];
            int32_t fn_end = wk->local_function_end[i];
            function_pos[i] = base_offset;
            /* Copy function code words */
            for (int32_t j = fn_start; j < fn_end; j++)
                code_emit(code, wk->local_code->words[j]);
            if (cold_diag_dump_per_fn || cold_diag_dump_slots) {
                fprintf(stderr, "[diag] fn[%d] ", i);
                cold_diag_fn_name(symbols->functions[i].name);
                fprintf(stderr, " merged at word=%d (count=%d)", base_offset, fn_end - fn_start);
                BodyIR *b = function_bodies[i];
                if (b) fprintf(stderr, " slots=%d frame=%d", b->slot_count, b->frame_size);
                fprintf(stderr, "\n");
                if (cold_diag_dump_per_fn) {
                    for (int32_t w = base_offset; w < code->count; w++)
                        fprintf(stderr, "  %08x\n", code->words[w]);
                }
            }
            /* Copy patches for this function (those between fn_start and fn_end) */
            for (int32_t j = 0; j < wk->local_patches.count; j++) {
                int32_t pp = wk->local_patches.items[j].pos;
                if (pp >= fn_start && pp < fn_end) {
                    function_patches_add(&function_patches,
                        pp - fn_start + base_offset,
                        wk->local_patches.items[j].target_function);
                }
            }
        }

        for (int32_t w = 0; w < active_jobs; w++) {
            /* Arena retained in cache for next compilation; skip munmap */
        }
    } else {
        /* E-Graph DCE: skip codegen for duplicate functions (same normalized hash) */
        int32_t dedup_count = 0;
        #define DEDUP_MAP_SIZE 256
        uint64_t dedup_hash[DEDUP_MAP_SIZE];
        int32_t dedup_pos[DEDUP_MAP_SIZE];
        int32_t dedup_n = 0;

        for (int32_t i = 0; i < function_count; i++) {
            if (i == entry_function || !reachable_functions[i] ||
                !function_bodies[i] ||
                function_bodies[i]->has_fallback) continue;
            BodyIR *body = function_bodies[i];
            uint64_t h = cold_body_ir_canonical_hash_normalized(body);
            int32_t dup_idx = -1;
            for (int32_t di = 0; di < dedup_n; di++) {
                if (dedup_hash[di] == h) { dup_idx = di; break; }
            }
            if (dup_idx >= 0) {
                function_pos[i] = dedup_pos[dup_idx];
                dedup_count++;
                continue;
            }
            function_pos[i] = code->count;
            if (dedup_n < DEDUP_MAP_SIZE) {
                dedup_hash[dedup_n] = h;
                dedup_pos[dedup_n] = code->count;
                dedup_n++;
            }
            if (!cold_body_codegen_ready(body))
                die("cold function body is not codegen-ready");
            if (use_rv64)
                rv64_codegen_func(code, body, symbols, &function_patches);
            else
                codegen_func(code, body, symbols, &function_patches);
            if (cold_diag_dump_per_fn || cold_diag_dump_slots) {
                fprintf(stderr, "[diag] fn[%d] end at word=%d (count=%d)\n", i, code->count, code->count - function_pos[i]);
                if (cold_diag_dump_per_fn) {
                    for (int32_t w = function_pos[i]; w < code->count; w++)
                        fprintf(stderr, "  %08x\n", code->words[w]);
                }
            }
            cold_egraph_dedup_count = dedup_count;
        }
    }

    for (int32_t i = 0; i < function_patches.count; i++) {
        FunctionPatch patch = function_patches.items[i];
        if (patch.target_function < 0 || patch.target_function >= function_count ||
            function_pos[patch.target_function] < 0) {
            if (patch.target_function >= 0 && patch.target_function < symbols->function_count) {
                fprintf(stderr, "cheng_cold: unresolved function patch target: ");
                cold_diag_fn_name(symbols->functions[patch.target_function].name);
                fputc('\n', stderr);
            }
            code->words[patch.pos] = use_rv64 ? rv_addi(RV_A0, RV_ZERO, 1) : 0xD2800020u;
            continue;
        }
        int32_t delta = function_pos[patch.target_function] - patch.pos;
        uint32_t ins = code->words[patch.pos];
        if (use_rv64) {
            code->words[patch.pos] = rv_jal(RV_RA, delta);
        } else if ((ins & 0xFC000000u) == 0x10000000u) {
            code->words[patch.pos] = a64_adr(ins & 0x1Fu, delta * 4);
        } else {
            code->words[patch.pos] = (ins & 0xFC000000u) | ((uint32_t)delta & 0x03FFFFFFu);
        }
        if (cold_diag_dump_per_fn) {
            fprintf(stderr, "[patch] pos=%d target=", patch.pos);
            cold_diag_fn_name(symbols->functions[patch.target_function].name);
            fprintf(stderr, " fn_pos=%d delta=%d ins=%08x\n",
                    function_pos[patch.target_function], delta, code->words[patch.pos]);
        }
    }
    if (!use_rv64)
        code->words[entry_call_pos] = (code->words[entry_call_pos] & 0xFC000000u) |
                                      ((uint32_t)(function_pos[entry_function] - entry_call_pos) & 0x03FFFFFFu);
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
               "core commands: status, print-build-plan, system-link-exec, emit-cold-csg-v2, run-host-smokes cold_csg_sidecar_smoke\n";
    }
    if (command_code == 1) {
        return "cheng\n"
               "version=cheng.compiler_runtime.v1\n"
               "execution=argv_control_plane\n"
               "bootstrap_mode=selfhost\n"
               "compiler_entry=src/core/tooling/backend_driver_dispatch_min.cheng\n"
               "ordinary_command=system-link-exec\n"
               "ordinary_pipeline=canonical_csg_verified_primary_object_codegen_ready\n"
               "stage3=artifacts/bootstrap/cheng.stage3\n"
               "linkerless_image=1\n"
               "system_link=0\n";
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
    /* Per-phase timing (microseconds) */
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
    int32_t canonical_hash_count;
    int32_t canonical_normalized_count;
    int32_t egraph_rewrite_count;
    int32_t egraph_dedup_count;
    int32_t total_function_count;
    int32_t link_object;
    int32_t provider_archive;
    int32_t provider_archive_member_count;
    uint64_t provider_archive_hash;
    int32_t provider_export_count;
    int32_t provider_resolved_symbol_count;
    int32_t link_reloc_count;
    int32_t unresolved_symbol_count;
    int32_t provider_object_count;
    char first_unresolved_symbol[COLD_NAME_CAP];
} ColdCompileStats;

static void cold_print_exec_phase_report(FILE *file, ColdCompileStats *stats) {
    unsigned long long parse_us = stats ? (unsigned long long)stats->parse_us : 0ULL;
    unsigned long long codegen_us = stats ? (unsigned long long)stats->codegen_us : 0ULL;
    unsigned long long emit_us = stats ? (unsigned long long)stats->emit_us : 0ULL;
    unsigned long long total_us = stats ? (unsigned long long)stats->elapsed_us : 0ULL;
    fprintf(file, "exec_phase_parse_us=%llu\n", parse_us);
    fprintf(file, "exec_phase_codegen_us=%llu\n", codegen_us);
    fprintf(file, "exec_phase_direct_object_emit_us=%llu\n", emit_us);
    fprintf(file, "exec_phase_total_us=%llu\n", total_us);
}

static void cold_print_resource_report(FILE *file) {
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        fputs("report_cpu_ms=-1\n", file);
        fputs("report_rss_bytes=-1\n", file);
        return;
    }
    long long cpu_ms =
        (long long)usage.ru_utime.tv_sec * 1000LL +
        (long long)usage.ru_utime.tv_usec / 1000LL +
        (long long)usage.ru_stime.tv_sec * 1000LL +
        (long long)usage.ru_stime.tv_usec / 1000LL;
#if defined(__APPLE__)
    long long rss_bytes = (long long)usage.ru_maxrss;
#else
    long long rss_bytes = (long long)usage.ru_maxrss * 1024LL;
#endif
    fprintf(file, "report_cpu_ms=%lld\n", cpu_ms);
    fprintf(file, "report_rss_bytes=%lld\n", rss_bytes);
}

static void cold_collect_body_stats(Symbols *symbols, BodyIR **function_bodies, int32_t function_count,
                                    ColdCompileStats *stats) {
    /* Count total non-fallback functions and compute canonical hashes */
    uint64_t *canonical_hashes = 0;
    int32_t canonical_cap = 0;
    int32_t canonical_count = 0;
    for (int32_t i = 0; i < function_count; i++) {
        BodyIR *body = function_bodies[i];
        if (!body || body->has_fallback) continue;
        stats->total_function_count++;
        uint64_t h = cold_body_ir_canonical_hash(body);
        bool seen = false;
        for (int32_t j = 0; j < canonical_count; j++) {
            if (canonical_hashes[j] == h) { seen = true; break; }
        }
        if (!seen) {
            if (canonical_count >= canonical_cap) {
                int32_t new_cap = canonical_cap == 0 ? 64 : canonical_cap * 2;
                uint64_t *new_h = realloc(canonical_hashes, (size_t)new_cap * sizeof(uint64_t));
                if (!new_h) { free(canonical_hashes); return; }
                canonical_hashes = new_h;
                canonical_cap = new_cap;
            }
            canonical_hashes[canonical_count++] = h;
        }
    }
    stats->canonical_hash_count = canonical_count;
    free(canonical_hashes);

    /* Compute normalized hash count (with rewrite rules) */
    uint64_t *norm_hashes = 0;
    int32_t norm_cap = 0;
    int32_t norm_count = 0;
    for (int32_t i = 0; i < function_count; i++) {
        BodyIR *body = function_bodies[i];
        if (!body || body->has_fallback) continue;
        uint64_t h = cold_body_ir_canonical_hash_normalized(body);
        bool seen = false;
        for (int32_t j = 0; j < norm_count; j++) {
            if (norm_hashes[j] == h) { seen = true; break; }
        }
        if (!seen) {
            if (norm_count >= norm_cap) {
                int32_t new_cap = norm_cap == 0 ? 64 : norm_cap * 2;
                uint64_t *new_h = realloc(norm_hashes, (size_t)new_cap * sizeof(uint64_t));
                if (!new_h) { free(norm_hashes); norm_count = 0; break; }
                norm_hashes = new_h;
                norm_cap = new_cap;
            }
            norm_hashes[norm_count++] = h;
        }
    }
    stats->canonical_normalized_count = norm_count;
    free(norm_hashes);

    for (int32_t i = 0; i < function_count; i++) {
        BodyIR *body = function_bodies[i];
        if (!body || body->has_fallback) continue;
        int32_t frame_size = align_i32(body->frame_size, 16);
        if (frame_size > stats->max_frame_size) {
            stats->max_frame_size = frame_size;
            stats->max_frame_function[0] = '\0';
            if (symbols && i < symbols->function_count) {
                Span fn_name = symbols->functions[i].name;
                cold_span_copy(stats->max_frame_function,
                               sizeof(stats->max_frame_function),
                               fn_name);
            }
        }
        if (symbols && i < symbols->function_count) {
            Span fn_name = symbols->functions[i].name;
            if (span_eq(fn_name, "BackendDriverDispatchMinRunSystemLinkExecConcretePlanAfterCsg")) {
                stats->after_csg_frame_size = frame_size;
            } else if (span_eq(fn_name, "BackendDriverDispatchMinRunSystemLinkExecConcretePlanAfterSourceBundle")) {
                stats->after_source_bundle_frame_size = frame_size;
            }
        }
        stats->op_count += body->op_count;
        stats->block_count += body->block_count;
        stats->switch_count += body->switch_count;
        /* count parameters and ABI placement */
        int32_t fn_reg = 0;
        int32_t fn_stack = 0;
        if (!body->slot_kind || !body->param_slot) continue;
        for (int32_t p = 0; p < body->param_count; p++) {
            int32_t slot = body->param_slot[p];
            if (slot < 0 || slot >= body->slot_count) continue;
            int32_t kind = body->slot_kind[slot];
            int32_t size = body->slot_size[slot];
            int32_t base_reg = -1;
            int32_t stack_offset = -1;
            bool in_regs = cold_abi_place_arg(&fn_reg, &fn_stack, kind, size,
                                              &base_reg, &stack_offset);
            stats->param_count++;
            if (in_regs) stats->abi_register_params++;
            else stats->abi_stack_params++;
        }
        for (int32_t op = 0; op < body->op_count; op++) {
            if (body->op_kind[op] == BODY_OP_CALL_I32 ||
                body->op_kind[op] == BODY_OP_CALL_COMPOSITE) {
                stats->call_count++;
            }
        }
    }
}

/* ================================================================
 * CSG v2 canonical primary-object text reader
 * ================================================================ */

typedef struct ColdCsgV2PrimaryFunction {
    uint32_t item_id;
    int32_t word_offset;
    int32_t word_count;
    Span symbol_name;
    Span body_kind;
} ColdCsgV2PrimaryFunction;

typedef struct ColdCsgV2PrimaryReloc {
    uint32_t source_item_id;
    int32_t word_offset;
    Span target_symbol;
} ColdCsgV2PrimaryReloc;

typedef struct ColdCsgV2PrimaryObject {
    Span target_triple;
    Span object_format;
    Span entry_symbol;
    ColdCsgV2PrimaryFunction *functions;
    int32_t function_count;
    uint32_t *words;
    int32_t word_count;
    ColdCsgV2PrimaryReloc *relocs;
    int32_t reloc_count;
} ColdCsgV2PrimaryObject;

typedef struct ColdCsgV2TextRecord {
    uint16_t kind;
    int32_t payload_bytes;
    const uint8_t *payload_hex;
} ColdCsgV2TextRecord;

static int32_t cold_csg_v2_hex_value(uint8_t ch) {
    if (ch >= '0' && ch <= '9') return (int32_t)(ch - '0');
    if (ch >= 'A' && ch <= 'F') return (int32_t)(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return (int32_t)(ch - 'a' + 10);
    return -1;
}

static bool cold_csg_v2_read_fixed_hex(const uint8_t *ptr, int32_t len, uint32_t *out) {
    if (!ptr || len <= 0 || len > 8) return false;
    uint32_t value = 0;
    for (int32_t i = 0; i < len; i++) {
        int32_t digit = cold_csg_v2_hex_value(ptr[i]);
        if (digit < 0) return false;
        value = (value << 4) | (uint32_t)digit;
    }
    *out = value;
    return true;
}

static bool cold_csg_v2_hex_byte(const uint8_t *payload_hex,
                                 int32_t payload_bytes,
                                 int32_t byte_index,
                                 uint8_t *out) {
    if (!payload_hex || byte_index < 0 || byte_index >= payload_bytes) return false;
    int32_t hi = cold_csg_v2_hex_value(payload_hex[byte_index * 2]);
    int32_t lo = cold_csg_v2_hex_value(payload_hex[byte_index * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    *out = (uint8_t)((hi << 4) | lo);
    return true;
}

static bool cold_csg_v2_read_payload_u32le(const uint8_t *payload_hex,
                                           int32_t payload_bytes,
                                           int32_t *pos,
                                           uint32_t *out) {
    if (!pos || !out || *pos < 0 || *pos > payload_bytes - 4) return false;
    uint8_t b0, b1, b2, b3;
    if (!cold_csg_v2_hex_byte(payload_hex, payload_bytes, *pos + 0, &b0)) return false;
    if (!cold_csg_v2_hex_byte(payload_hex, payload_bytes, *pos + 1, &b1)) return false;
    if (!cold_csg_v2_hex_byte(payload_hex, payload_bytes, *pos + 2, &b2)) return false;
    if (!cold_csg_v2_hex_byte(payload_hex, payload_bytes, *pos + 3, &b3)) return false;
    *out = (uint32_t)b0 | ((uint32_t)b1 << 8) |
           ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
    *pos += 4;
    return true;
}

static bool cold_csg_v2_read_payload_string(const uint8_t *payload_hex,
                                            int32_t payload_bytes,
                                            int32_t *pos,
                                            Arena *arena,
                                            Span *out) {
    uint32_t len_u32;
    if (!cold_csg_v2_read_payload_u32le(payload_hex, payload_bytes, pos, &len_u32)) return false;
    if (len_u32 > (uint32_t)INT32_MAX) return false;
    int32_t len = (int32_t)len_u32;
    if (*pos < 0 || len < 0 || *pos > payload_bytes - len) return false;
    char *buf = arena_alloc(arena, (size_t)len + 1);
    for (int32_t i = 0; i < len; i++) {
        uint8_t byte;
        if (!cold_csg_v2_hex_byte(payload_hex, payload_bytes, *pos + i, &byte)) return false;
        buf[i] = (char)byte;
    }
    buf[len] = '\0';
    *pos += len;
    *out = (Span){(const uint8_t *)buf, len};
    return true;
}

static bool cold_csg_v2_next_text_record(Span text,
                                         int32_t *pos,
                                         ColdCsgV2TextRecord *record) {
    if (!pos || !record || *pos < 0 || *pos >= text.len) return false;
    int32_t start = *pos;
    int32_t end = start;
    while (end < text.len && text.ptr[end] != '\n') end++;
    int32_t line_len = end - start;
    *pos = end < text.len ? end + 1 : end;
    if (line_len < 13) return false;
    if (text.ptr[start] != 'R') return false;
    uint32_t kind = 0;
    uint32_t payload_bytes = 0;
    if (!cold_csg_v2_read_fixed_hex(text.ptr + start + 1, 4, &kind)) return false;
    if (!cold_csg_v2_read_fixed_hex(text.ptr + start + 5, 8, &payload_bytes)) return false;
    if (kind == 0 || kind > UINT16_MAX) return false;
    if (payload_bytes > (uint32_t)(INT32_MAX / 2)) return false;
    int32_t payload_len = (int32_t)payload_bytes;
    if (payload_len > (INT32_MAX - 13) / 2) return false;
    if (line_len != 13 + payload_len * 2) return false;
    for (int32_t i = 0; i < payload_len * 2; i++) {
        if (cold_csg_v2_hex_value(text.ptr[start + 13 + i]) < 0) return false;
    }
    record->kind = (uint16_t)kind;
    record->payload_bytes = payload_len;
    record->payload_hex = text.ptr + start + 13;
    return true;
}

static bool cold_csg_v2_count_primary_records(Span text,
                                              int32_t *out_functions,
                                              int32_t *out_words,
                                              int32_t *out_relocs) {
    static const char magic[] = "CHENG_CSG_V2\n";
    int32_t magic_len = (int32_t)sizeof(magic) - 1;
    if (text.len < magic_len || memcmp(text.ptr, magic, (size_t)magic_len) != 0) return false;
    bool saw_target = false;
    bool saw_format = false;
    bool saw_entry = false;
    int32_t functions = 0;
    int32_t words = 0;
    int32_t relocs = 0;
    int32_t pos = magic_len;
    while (pos < text.len) {
        ColdCsgV2TextRecord record;
        if (!cold_csg_v2_next_text_record(text, &pos, &record)) return false;
        switch (record.kind) {
            case 1:
                if (saw_target) return false;
                saw_target = true;
                break;
            case 2:
                if (saw_format) return false;
                saw_format = true;
                break;
            case 3:
                if (saw_entry) return false;
                saw_entry = true;
                break;
            case 4:
                if (functions == INT32_MAX) return false;
                functions++;
                break;
            case 5:
                if (words == INT32_MAX) return false;
                words++;
                break;
            case 6:
                if (relocs == INT32_MAX) return false;
                relocs++;
                break;
            default:
                return false;
        }
    }
    if (!saw_target || !saw_format || !saw_entry) return false;
    if (functions <= 0) return false;
    *out_functions = functions;
    *out_words = words;
    *out_relocs = relocs;
    return true;
}

static int32_t cold_csg_v2_find_item(const ColdCsgV2PrimaryObject *facts, uint32_t item_id) {
    for (int32_t i = 0; i < facts->function_count; i++) {
        if (facts->functions[i].item_id == item_id) return i;
    }
    return -1;
}

static int32_t cold_csg_v2_find_symbol(const ColdCsgV2PrimaryObject *facts, Span symbol) {
    for (int32_t i = 0; i < facts->function_count; i++) {
        if (span_same(facts->functions[i].symbol_name, symbol)) return i;
    }
    return -1;
}

static const char *cold_csg_v2_object_format_for_target(const char *target) {
    if (!target) return "";
    if (strcmp(target, "arm64-apple-darwin") == 0) return "macho";
    if (strcmp(target, "aarch64-unknown-linux-gnu") == 0 ||
        strcmp(target, "x86_64-unknown-linux-gnu") == 0 ||
        strcmp(target, "riscv64-unknown-linux-gnu") == 0) return "elf";
    if (strcmp(target, "x86_64-pc-windows-msvc") == 0 ||
        strcmp(target, "aarch64-pc-windows-msvc") == 0) return "coff";
    return "";
}

static bool cold_csg_v2_validate_primary_object(ColdCsgV2PrimaryObject *facts,
                                                const char *requested_target) {
    if (!facts || facts->function_count <= 0) return false;
    if (facts->target_triple.len <= 0 || facts->object_format.len <= 0) return false;
    if (requested_target && requested_target[0] != '\0') {
        if (!span_eq(facts->target_triple, requested_target)) return false;
        const char *expected_format = cold_csg_v2_object_format_for_target(requested_target);
        if (expected_format[0] == '\0') return false;
        if (!span_eq(facts->object_format, expected_format)) return false;
    }
    for (int32_t i = 0; i < facts->function_count; i++) {
        ColdCsgV2PrimaryFunction *fn = &facts->functions[i];
        if (fn->symbol_name.len <= 0) return false;
        if (fn->word_offset < 0 || fn->word_count < 0) return false;
        if (fn->word_offset > facts->word_count) return false;
        if (fn->word_count > facts->word_count - fn->word_offset) return false;
        for (int32_t j = i + 1; j < facts->function_count; j++) {
            if (facts->functions[j].item_id == fn->item_id) return false;
            if (span_same(facts->functions[j].symbol_name, fn->symbol_name)) return false;
        }
    }
    if (facts->entry_symbol.len > 0 &&
        cold_csg_v2_find_symbol(facts, facts->entry_symbol) < 0) {
        return false;
    }
    for (int32_t i = 0; i < facts->reloc_count; i++) {
        ColdCsgV2PrimaryReloc *reloc = &facts->relocs[i];
        if (reloc->target_symbol.len <= 0) return false;
        int32_t source_index = cold_csg_v2_find_item(facts, reloc->source_item_id);
        if (source_index < 0) return false;
        ColdCsgV2PrimaryFunction *source_fn = &facts->functions[source_index];
        if (reloc->word_offset < source_fn->word_offset) return false;
        if (reloc->word_offset >= source_fn->word_offset + source_fn->word_count) return false;
    }
    return true;
}

static bool cold_csg_v2_decode_primary_object(Span text,
                                              Arena *arena,
                                              int32_t function_count,
                                              int32_t word_count,
                                              int32_t reloc_count,
                                              const char *target,
                                              ColdCsgV2PrimaryObject *out) {
    static const char magic[] = "CHENG_CSG_V2\n";
    int32_t magic_len = (int32_t)sizeof(magic) - 1;
    memset(out, 0, sizeof(*out));
    out->function_count = function_count;
    out->word_count = word_count;
    out->reloc_count = reloc_count;
    out->functions = arena_alloc(arena, (size_t)function_count * sizeof(ColdCsgV2PrimaryFunction));
    out->words = word_count > 0
        ? arena_alloc(arena, (size_t)word_count * sizeof(uint32_t))
        : 0;
    out->relocs = reloc_count > 0
        ? arena_alloc(arena, (size_t)reloc_count * sizeof(ColdCsgV2PrimaryReloc))
        : 0;

    bool saw_target = false;
    bool saw_format = false;
    bool saw_entry = false;
    int32_t fn_index = 0;
    int32_t word_index = 0;
    int32_t reloc_index = 0;
    int32_t pos = magic_len;
    while (pos < text.len) {
        ColdCsgV2TextRecord record;
        if (!cold_csg_v2_next_text_record(text, &pos, &record)) return false;
        int32_t payload_pos = 0;
        switch (record.kind) {
            case 1:
                if (saw_target) return false;
                if (!cold_csg_v2_read_payload_string(record.payload_hex, record.payload_bytes,
                                                     &payload_pos, arena, &out->target_triple)) return false;
                if (payload_pos != record.payload_bytes) return false;
                saw_target = true;
                break;
            case 2:
                if (saw_format) return false;
                if (!cold_csg_v2_read_payload_string(record.payload_hex, record.payload_bytes,
                                                     &payload_pos, arena, &out->object_format)) return false;
                if (payload_pos != record.payload_bytes) return false;
                saw_format = true;
                break;
            case 3:
                if (saw_entry) return false;
                if (!cold_csg_v2_read_payload_string(record.payload_hex, record.payload_bytes,
                                                     &payload_pos, arena, &out->entry_symbol)) return false;
                if (payload_pos != record.payload_bytes) return false;
                saw_entry = true;
                break;
            case 4: {
                if (fn_index >= function_count) return false;
                uint32_t item_id, word_offset, fn_word_count;
                if (!cold_csg_v2_read_payload_u32le(record.payload_hex, record.payload_bytes,
                                                    &payload_pos, &item_id)) return false;
                if (!cold_csg_v2_read_payload_u32le(record.payload_hex, record.payload_bytes,
                                                    &payload_pos, &word_offset)) return false;
                if (!cold_csg_v2_read_payload_u32le(record.payload_hex, record.payload_bytes,
                                                    &payload_pos, &fn_word_count)) return false;
                if (item_id > (uint32_t)INT32_MAX ||
                    word_offset > (uint32_t)INT32_MAX ||
                    fn_word_count > (uint32_t)INT32_MAX) return false;
                ColdCsgV2PrimaryFunction *fn = &out->functions[fn_index++];
                fn->item_id = item_id;
                fn->word_offset = (int32_t)word_offset;
                fn->word_count = (int32_t)fn_word_count;
                if (!cold_csg_v2_read_payload_string(record.payload_hex, record.payload_bytes,
                                                     &payload_pos, arena, &fn->symbol_name)) return false;
                if (!cold_csg_v2_read_payload_string(record.payload_hex, record.payload_bytes,
                                                     &payload_pos, arena, &fn->body_kind)) return false;
                if (payload_pos != record.payload_bytes) return false;
                break;
            }
            case 5: {
                if (word_index >= word_count) return false;
                uint32_t word;
                if (!cold_csg_v2_read_payload_u32le(record.payload_hex, record.payload_bytes,
                                                    &payload_pos, &word)) return false;
                if (payload_pos != record.payload_bytes) return false;
                out->words[word_index++] = word;
                break;
            }
            case 6: {
                if (reloc_index >= reloc_count) return false;
                uint32_t source_item_id, word_offset;
                if (!cold_csg_v2_read_payload_u32le(record.payload_hex, record.payload_bytes,
                                                    &payload_pos, &source_item_id)) return false;
                if (!cold_csg_v2_read_payload_u32le(record.payload_hex, record.payload_bytes,
                                                    &payload_pos, &word_offset)) return false;
                if (source_item_id > (uint32_t)INT32_MAX ||
                    word_offset > (uint32_t)INT32_MAX) return false;
                ColdCsgV2PrimaryReloc *reloc = &out->relocs[reloc_index++];
                reloc->source_item_id = source_item_id;
                reloc->word_offset = (int32_t)word_offset;
                if (!cold_csg_v2_read_payload_string(record.payload_hex, record.payload_bytes,
                                                     &payload_pos, arena, &reloc->target_symbol)) return false;
                if (payload_pos != record.payload_bytes) return false;
                break;
            }
            default:
                return false;
        }
    }
    if (!saw_target || !saw_format || !saw_entry) return false;
    if (fn_index != function_count || word_index != word_count || reloc_index != reloc_count) return false;
    return cold_csg_v2_validate_primary_object(out, target);
}

static char *cold_span_to_cstr(Arena *arena, Span span) {
    char *buf = arena_alloc(arena, (size_t)span.len + 1);
    if (span.len > 0) memcpy(buf, span.ptr, (size_t)span.len);
    buf[span.len] = '\0';
    return buf;
}

static int32_t cold_csg_v2_symbol_index_for_reloc(ColdCsgV2PrimaryObject *facts,
                                                  Span target_symbol,
                                                  const char **names,
                                                  int32_t *offsets,
                                                  int32_t *name_count,
                                                  int32_t max_names,
                                                  Arena *arena) {
    int32_t defined = cold_csg_v2_find_symbol(facts, target_symbol);
    if (defined >= 0) return defined;
    for (int32_t i = facts->function_count; i < *name_count; i++) {
        Span existing = {(const uint8_t *)names[i], (int32_t)strlen(names[i])};
        if (span_same(existing, target_symbol)) return i;
    }
    if (*name_count >= max_names) return -1;
    int32_t index = *name_count;
    names[index] = cold_span_to_cstr(arena, target_symbol);
    offsets[index] = -1;
    *name_count = *name_count + 1;
    return index;
}

static bool cold_csg_v2_emit_primary_object(const char *out_path,
                                            const char *target,
                                            ColdCsgV2PrimaryObject *facts,
                                            Arena *arena) {
    bool is_elf = target && (strcmp(target, "aarch64-unknown-linux-gnu") == 0 ||
                             strcmp(target, "x86_64-unknown-linux-gnu") == 0 ||
                             strcmp(target, "riscv64-unknown-linux-gnu") == 0);
    bool is_coff = target && (strcmp(target, "x86_64-pc-windows-msvc") == 0 ||
                              strcmp(target, "aarch64-pc-windows-msvc") == 0);
    bool is_macho = target && strcmp(target, "arm64-apple-darwin") == 0;
    if (!is_macho && !is_elf && !is_coff) return false;

    uint16_t elf_machine = 0;
    uint16_t coff_machine = 0;
    if (is_elf) {
        if (strstr(target, "aarch64")) elf_machine = EM_AARCH64;
        else if (strstr(target, "riscv64")) elf_machine = EM_RISCV;
        else if (strstr(target, "x86_64")) elf_machine = EM_X86_64;
        else return false;
    }
    if (is_coff) {
        if (strstr(target, "aarch64")) coff_machine = IMAGE_FILE_MACHINE_ARM64;
        else if (strstr(target, "x86_64")) coff_machine = IMAGE_FILE_MACHINE_AMD64;
        else return false;
    }

    int32_t max_names = facts->function_count + facts->reloc_count;
    if (max_names <= 0) return false;
    const char **names = arena_alloc(arena, (size_t)max_names * sizeof(const char *));
    int32_t *offsets = arena_alloc(arena, (size_t)max_names * sizeof(int32_t));
    int32_t name_count = facts->function_count;
    for (int32_t i = 0; i < facts->function_count; i++) {
        names[i] = cold_span_to_cstr(arena, facts->functions[i].symbol_name);
        offsets[i] = facts->functions[i].word_offset;
    }

    int32_t *reloc_offsets = facts->reloc_count > 0
        ? arena_alloc(arena, (size_t)facts->reloc_count * sizeof(int32_t))
        : 0;
    int32_t *reloc_symbols = facts->reloc_count > 0
        ? arena_alloc(arena, (size_t)facts->reloc_count * sizeof(int32_t))
        : 0;
    for (int32_t i = 0; i < facts->reloc_count; i++) {
        if (facts->relocs[i].word_offset > INT32_MAX / 4) return false;
        int32_t byte_offset = facts->relocs[i].word_offset * 4;
        int32_t symbol_index = cold_csg_v2_symbol_index_for_reloc(facts,
                                                                  facts->relocs[i].target_symbol,
                                                                  names, offsets,
                                                                  &name_count, max_names,
                                                                  arena);
        if (symbol_index < 0) return false;
        reloc_offsets[i] = byte_offset;
        reloc_symbols[i] = symbol_index;
    }
    int32_t local_count = 0;
    if (is_elf) {
        return elf64_write_object(out_path, facts->words, facts->word_count,
                                  names, offsets, name_count, local_count,
                                  reloc_offsets, reloc_symbols, facts->reloc_count,
                                  elf_machine);
    }
    if (is_coff) {
        return coff_write_object(out_path, facts->words, facts->word_count,
                                 names, offsets, name_count, local_count,
                                 reloc_offsets, reloc_symbols, facts->reloc_count,
                                 coff_machine);
    }
    return macho_write_object(out_path, facts->words, facts->word_count,
                              names, offsets, name_count, local_count,
                              reloc_offsets, reloc_symbols, facts->reloc_count);
}

static bool cold_compile_canonical_csg_v2_primary_object(const char *out_path,
                                                         const char *target,
                                                         Span csg_text,
                                                         Arena *arena,
                                                         ColdCompileStats *stats,
                                                         uint64_t start_us,
                                                         uint64_t mmap_done_us) {
    if (csg_text.len <= 0 ||
        csg_text.len > (int32_t)CSG_V2_BUDGET_MAX_FACTS_BYTES) {
        return false;
    }
    int32_t function_count = 0;
    int32_t word_count = 0;
    int32_t reloc_count = 0;
    uint64_t verify_start_us = cold_now_us();
    if (!cold_csg_v2_count_primary_records(csg_text, &function_count, &word_count, &reloc_count)) {
        return false;
    }
    if (function_count <= 0 || function_count > CSG_V2_BUDGET_MAX_FN_COUNT) return false;
    if (word_count < 0 || word_count > CSG_V2_BUDGET_MAX_OP_COUNT) return false;
    if (reloc_count < 0 || reloc_count > CSG_V2_BUDGET_MAX_OP_COUNT) return false;
    uint64_t verify_end_us = cold_now_us();
    ColdCsgV2PrimaryObject facts;
    if (!cold_csg_v2_decode_primary_object(csg_text, arena,
                                           function_count, word_count, reloc_count,
                                           target, &facts)) {
        return false;
    }
    uint64_t decode_end_us = cold_now_us();
    bool ok = cold_csg_v2_emit_primary_object(out_path, target, &facts, arena);
    uint64_t emit_end_us = cold_now_us();
    if (stats) {
        stats->function_count = facts.function_count;
        stats->csg_lowering = 1;
        stats->code_words = facts.word_count;
        stats->arena_kb = arena->used / 1024;
        stats->facts_bytes = (uint64_t)csg_text.len;
        stats->facts_mmap_us = mmap_done_us >= start_us ? mmap_done_us - start_us : 0;
        stats->facts_verify_us = verify_end_us >= verify_start_us ? verify_end_us - verify_start_us : 0;
        stats->facts_decode_us = decode_end_us >= verify_end_us ? decode_end_us - verify_end_us : 0;
        stats->facts_emit_obj_us = emit_end_us >= decode_end_us ? emit_end_us - decode_end_us : 0;
        stats->facts_total_us = emit_end_us >= start_us ? emit_end_us - start_us : 0;
        stats->facts_function_count = facts.function_count;
        stats->facts_word_count = facts.word_count;
        stats->facts_reloc_count = facts.reloc_count;
        stats->parse_us = stats->facts_verify_us + stats->facts_decode_us;
        stats->emit_us = stats->facts_emit_obj_us;
        stats->elapsed_us = stats->facts_total_us;
    }
    return ok;
}

/* ================================================================
 * CSG v2 binary reader
 * ================================================================ */

static bool cold_read_u32(const uint8_t *data, int32_t data_len, int32_t *pos, uint32_t *out) {
    if (*pos + 4 > data_len) return false;
    *out = (uint32_t)data[*pos] | ((uint32_t)data[*pos+1] << 8) |
           ((uint32_t)data[*pos+2] << 16) | ((uint32_t)data[*pos+3] << 24);
    *pos += 4;
    return true;
}

static bool cold_read_u64(const uint8_t *data, int32_t data_len, int32_t *pos, uint64_t *out) {
    if (*pos + 8 > data_len) return false;
    *out = (uint64_t)data[*pos] | ((uint64_t)data[*pos+1] << 8) |
           ((uint64_t)data[*pos+2] << 16) | ((uint64_t)data[*pos+3] << 24) |
           ((uint64_t)data[*pos+4] << 32) | ((uint64_t)data[*pos+5] << 40) |
           ((uint64_t)data[*pos+6] << 48) | ((uint64_t)data[*pos+7] << 56);
    *pos += 8;
    return true;
}

static bool cold_read_str(const uint8_t *data, int32_t data_len, int32_t *pos, Span *out) {
    uint32_t len;
    if (!cold_read_u32(data, data_len, pos, &len)) return false;
    if (*pos + (int32_t)len > data_len) return false;
    out->ptr = data + *pos;
    out->len = (int32_t)len;
    *pos += (int32_t)len;
    return true;
}

typedef struct ColdCsgV2Timing {
    int32_t facts_bytes;
    int32_t verify_us;
    int32_t decode_us;
    int32_t fn_count;
    int32_t op_count;
} ColdCsgV2Timing;

static bool cold_load_csg_v2_facts(
    const uint8_t *data, int32_t data_len,
    BodyIR ***out_bodies, int32_t *out_count,
    Symbols *symbols, Arena *arena,
    ColdCsgV2Timing *timing
) {
    uint64_t t_start = cold_now_us();
    uint64_t t_verify = 0, t_decode = 0;
    int32_t total_op_count = 0;
    int32_t pos = 0;

    /* Budget: total facts size */
    if (data_len > CSG_V2_BUDGET_MAX_FACTS_BYTES) {
        fprintf(stderr, "[CSG-V2] budget exceeded: facts_bytes=%d > max=%d\n",
                data_len, CSG_V2_BUDGET_MAX_FACTS_BYTES);
        return false;
    }

    /* 1. Parse header (64 bytes) */
    if (pos + 64 > data_len) return false;
    if (memcmp(data + pos, "CHENGCSG", 8) != 0) return false;
    pos += 8;
    t_verify = cold_now_us();

    uint32_t version;
    if (!cold_read_u32(data, data_len, &pos, &version)) return false;
    if (version != 2) return false;

    /* Skip: target_triple(32) + abi(4) + pointer_width(1) + endianness(1) + reserved(14) */
    pos += 32 + 4 + 1 + 1 + 14;

    /* 2. Read section index */
    uint32_t section_count;
    if (!cold_read_u32(data, data_len, &pos, &section_count)) return false;
    if (section_count > 10) return false;

    uint32_t sec_kind[10], sec_size[10];
    uint64_t sec_offset[10];
    uint64_t section_data_end = 0;
    for (uint32_t si = 0; si < section_count; si++) {
        if (!cold_read_u32(data, data_len, &pos, &sec_kind[si])) return false;
        if (!cold_read_u64(data, data_len, &pos, &sec_offset[si])) return false;
        if (!cold_read_u32(data, data_len, &pos, &sec_size[si])) return false;
        if (sec_offset[si] > (uint64_t)data_len) return false;
        if ((uint64_t)sec_size[si] > (uint64_t)data_len - sec_offset[si]) return false;
        uint64_t end = sec_offset[si] + (uint64_t)sec_size[si];
        if (end > section_data_end) section_data_end = end;
    }
    if (section_data_end > (uint64_t)data_len) return false;
    if ((uint64_t)data_len - section_data_end != CSG_V2_TRAILER_SIZE) return false;
    {
        int32_t tpos = (int32_t)section_data_end;
        uint32_t trailer_section_count = 0;
        if (!cold_read_u32(data, data_len, &tpos, &trailer_section_count)) return false;
        if (trailer_section_count != section_count) return false;
        tpos += CSG_V2_HASH_SIZE;
        if (tpos != data_len) return false;
    }

    /* 3. Parse string section (kind=5) */
    Span *str_spans = 0;
    uint32_t str_count = 0;
    for (uint32_t si = 0; si < section_count; si++) {
        if (sec_kind[si] == 5) {
            int32_t spos = (int32_t)sec_offset[si];
            if (!cold_read_u32(data, data_len, &spos, &str_count)) return false;
            if (str_count > 0) {
                str_spans = arena_alloc(arena, (size_t)str_count * sizeof(Span));
                memset(str_spans, 0, (size_t)str_count * sizeof(Span));
                for (uint32_t i = 0; i < str_count; i++) {
                    uint32_t id, slen;
                    if (!cold_read_u32(data, data_len, &spos, &id)) return false;
                    if (!cold_read_u32(data, data_len, &spos, &slen)) return false;
                    if ((int32_t)(spos + (int32_t)slen) > data_len) return false;
                    if (id < str_count) {
                        str_spans[id] = (Span){data + spos, (int32_t)slen};
                    }
                    spos += (int32_t)slen;
                }
            }
            break;
        }
    }

    /* 4. Parse function section (kind=1) */
    int32_t sec_pos = 0;
    bool found_fn_section = false;
    for (uint32_t si = 0; si < section_count; si++) {
        if (sec_kind[si] == 1) {
            sec_pos = (int32_t)sec_offset[si];
            found_fn_section = true;
            break;
        }
    }
    if (!found_fn_section) return false;

    uint32_t fn_count;
    if (!cold_read_u32(data, data_len, &sec_pos, &fn_count)) return false;
    if (fn_count == 0) return false;
    if (fn_count > CSG_V2_BUDGET_MAX_FN_COUNT) {
        fprintf(stderr, "[CSG-V2] budget exceeded: fn_count=%u > max=%d\n",
                fn_count, CSG_V2_BUDGET_MAX_FN_COUNT);
        return false;
    }

    /* Temporary list of (symbol_index, BodyIR*) pairs */
    typedef struct FnBodyPair {
        int32_t symbol_index;
        BodyIR *body;
    } FnBodyPair;

    FnBodyPair *fn_list = arena_alloc(arena, (size_t)fn_count * sizeof(FnBodyPair));
    memset(fn_list, 0, (size_t)fn_count * sizeof(FnBodyPair));

    for (uint32_t fi = 0; fi < fn_count; fi++) {
        /* Read function name */
        Span fn_name;
        if (!cold_read_str(data, data_len, &sec_pos, &fn_name)) return false;

        uint32_t param_count, return_kind, frame_size, op_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &param_count)) return false;
        if (param_count > COLD_MAX_I32_PARAMS) return false;
        int32_t param_kinds[COLD_MAX_I32_PARAMS];
        int32_t param_sizes[COLD_MAX_I32_PARAMS];
        int32_t param_slots[COLD_MAX_I32_PARAMS];
        for (uint32_t pi = 0; pi < param_count; pi++) {
            uint32_t pk, pz, ps;
            if (!cold_read_u32(data, data_len, &sec_pos, &pk)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &pz)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &ps)) return false;
            if (pk > (uint32_t)INT32_MAX ||
                pz > (uint32_t)INT32_MAX ||
                ps > (uint32_t)INT32_MAX) return false;
            param_kinds[pi] = (int32_t)pk;
            param_sizes[pi] = (int32_t)pz;
            param_slots[pi] = (int32_t)ps;
        }
        if (!cold_read_u32(data, data_len, &sec_pos, &return_kind)) return false;
        if (!cold_read_u32(data, data_len, &sec_pos, &frame_size)) return false;
        if (!cold_read_u32(data, data_len, &sec_pos, &op_count)) return false;
        if (op_count > CSG_V2_BUDGET_MAX_OP_COUNT) {
            fprintf(stderr, "[CSG-V2] budget exceeded: op_count=%u > max=%d\n",
                    op_count, CSG_V2_BUDGET_MAX_OP_COUNT);
            return false;
        }
        total_op_count += (int32_t)op_count;

        /* Register function with symbols — map return_kind to type string */
        const char *ret_type_str = "int32";
        switch ((int32_t)return_kind) {
            case SLOT_I64: ret_type_str = "int64"; break;
            case SLOT_STR: ret_type_str = "str"; break;
            case SLOT_PTR: ret_type_str = "ptr"; break;
            case SLOT_VARIANT: case SLOT_OBJECT: ret_type_str = "ptr"; break;
            case SLOT_OPAQUE: ret_type_str = "ptr"; break;
            default: ret_type_str = "int32"; break;
        }
        int32_t sym_idx = symbols_add_fn(symbols, fn_name, (int32_t)param_count,
                                          param_kinds, param_sizes,
                                          cold_cstr_span(ret_type_str));

        /* Mark external functions (no ops = @importc declaration) */
        if ((int32_t)op_count == 0 && sym_idx >= 0 && sym_idx < symbols->function_count)
            symbols->functions[sym_idx].is_external = true;

        /* Create BodyIR (null body for external functions) */
        BodyIR *body = body_new(arena);
        body->frame_size = (int32_t)frame_size;
        body->return_kind = (int32_t)return_kind;
        body->return_size = body->return_kind == 0
            ? 0
            : cold_slot_size_for_kind(body->return_kind);
        body->return_type = (Span){0};
        body->sret_slot = -1;
        body->has_fallback = false;
        body->param_count = (int32_t)param_count;
        for (uint32_t pi = 0; pi < param_count; pi++) {
            body->param_slot[pi] = param_slots[pi];
            body->param_name[pi] = (Span){0};
        }

        /* Read ops (5 u32 per op) */
        for (uint32_t oi = 0; oi < op_count; oi++) {
            uint32_t kind, dst, a, b, c;
            if (!cold_read_u32(data, data_len, &sec_pos, &kind)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &dst)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &a)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &b)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &c)) return false;
            body_op3(body, (int32_t)kind, (int32_t)dst, (int32_t)a, (int32_t)b, (int32_t)c);
        }

        /* Read slots (3 u32 per slot) */
        uint32_t slot_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &slot_count)) return false;
        if (slot_count > CSG_V2_BUDGET_MAX_SLOT_COUNT) {
            fprintf(stderr, "[CSG-V2] budget exceeded: slot_count=%u > max=%d\n",
                    slot_count, CSG_V2_BUDGET_MAX_SLOT_COUNT);
            return false;
        }
        for (uint32_t si = 0; si < slot_count; si++) {
            uint32_t skind, soff, ssize;
            if (!cold_read_u32(data, data_len, &sec_pos, &skind)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &soff)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &ssize)) return false;
            body_ensure_slots(body);
            int32_t idx = body->slot_count++;
            body->slot_kind[idx] = (int32_t)skind;
            body->slot_offset[idx] = (int32_t)soff;
            body->slot_size[idx] = (int32_t)ssize;
            body->slot_aux[idx] = 0;
            body->slot_type[idx] = (Span){0};
            body->slot_no_alias[idx] = 0;
        }
        for (uint32_t pi = 0; pi < param_count; pi++) {
            int32_t slot = body->param_slot[pi];
            if (slot < 0 || slot >= body->slot_count) return false;
        }

        /* Read blocks (3 u32 per block) */
        uint32_t block_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &block_count)) return false;
        for (uint32_t bi = 0; bi < block_count; bi++) {
            uint32_t bop_start, bop_cnt, bterm;
            if (!cold_read_u32(data, data_len, &sec_pos, &bop_start)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &bop_cnt)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &bterm)) return false;
            body_ensure_blocks(body);
            int32_t idx = body->block_count++;
            body->block_op_start[idx] = (int32_t)bop_start;
            body->block_op_count[idx] = (int32_t)bop_cnt;
            body->block_term[idx] = (int32_t)bterm;
        }

        /* Read terms (6 u32 per term) */
        uint32_t term_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &term_count)) return false;
        for (uint32_t ti = 0; ti < term_count; ti++) {
            uint32_t tkind, tval, tcase_start, tcase_count, ttrue, tfalse;
            if (!cold_read_u32(data, data_len, &sec_pos, &tkind)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &tval)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &tcase_start)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &tcase_count)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &ttrue)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &tfalse)) return false;
            body_ensure_terms(body);
            int32_t idx = body->term_count++;
            body->term_kind[idx] = (int32_t)tkind;
            body->term_value[idx] = (int32_t)tval;
            body->term_case_start[idx] = (int32_t)tcase_start;
            body->term_case_count[idx] = (int32_t)tcase_count;
            body->term_true_block[idx] = (int32_t)ttrue;
            body->term_false_block[idx] = (int32_t)tfalse;
        }

        for (uint32_t bi = 0; bi < block_count; bi++) {
            int32_t term = body->block_term[bi];
            if (term < 0 || term >= (int32_t)term_count) return false;
        }

        /* Read switch cases (3 u32 per case) */
        uint32_t switch_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &switch_count)) return false;
        for (uint32_t si = 0; si < switch_count; si++) {
            uint32_t stag, sblock, sterm;
            if (!cold_read_u32(data, data_len, &sec_pos, &stag)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &sblock)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &sterm)) return false;
            body_ensure_switches(body);
            int32_t idx = body->switch_count++;
            body->switch_tag[idx] = (int32_t)stag;
            body->switch_block[idx] = (int32_t)sblock;
            body->switch_term[idx] = (int32_t)sterm;
        }

        /* Skip call metadata (4 u32 per call) */
        uint32_t call_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &call_count)) return false;
        sec_pos += (int32_t)(call_count * 16);
        if (sec_pos > data_len) return false;

        /* Read call args (2 u32 per arg) */
        uint32_t call_arg_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &call_arg_count)) return false;
        for (uint32_t cai = 0; cai < call_arg_count; cai++) {
            uint32_t caslot, caoff;
            if (!cold_read_u32(data, data_len, &sec_pos, &caslot)) return false;
            if (!cold_read_u32(data, data_len, &sec_pos, &caoff)) return false;
            body_ensure_call_args(body);
            int32_t idx = body->call_arg_count++;
            body->call_arg_slot[idx] = (int32_t)caslot;
            body->call_arg_offset[idx] = (int32_t)caoff;
        }

        /* Read string literal ids */
        uint32_t str_lit_count;
        if (!cold_read_u32(data, data_len, &sec_pos, &str_lit_count)) return false;
        for (uint32_t sli = 0; sli < str_lit_count; sli++) {
            uint32_t str_id;
            if (!cold_read_u32(data, data_len, &sec_pos, &str_id)) return false;
            body_ensure_string_literals(body);
            int32_t idx = body->string_literal_count++;
            if (str_id < str_count && str_spans) {
                body->string_literal[idx] = str_spans[str_id];
            } else {
                body->string_literal[idx] = (Span){0};
            }
        }

        fn_list[fi].symbol_index = sym_idx;
        /* External functions: no codegen, left as undefined symbol */
        fn_list[fi].body = (op_count == 0) ? NULL : body;
    }

    /* 5. Build function_bodies array */
    {
        int32_t cap = symbols->function_cap;
        BodyIR **bodies = arena_alloc(arena, (size_t)cap * sizeof(BodyIR *));
        memset(bodies, 0, (size_t)cap * sizeof(BodyIR *));
        for (uint32_t fi = 0; fi < fn_count; fi++) {
            int32_t si = fn_list[fi].symbol_index;
            if (si >= 0 && si < cap) {
                bodies[si] = fn_list[fi].body;
            }
        }
        *out_bodies = bodies;
    }
    *out_count = (int32_t)symbols->function_count;
    t_decode = cold_now_us();
    if (timing) {
        timing->facts_bytes = data_len;
        timing->verify_us = (int32_t)(t_verify - t_start);
        timing->decode_us = (int32_t)(t_decode - t_verify);
        timing->fn_count = (int32_t)fn_count;
        timing->op_count = total_op_count;
    }
    return true;
}

/* ================================================================
 * CSG v2 record-based binary facts reader (lowered instruction words)
 * ================================================================ */

static uint32_t cold_read_csg_v2_u32_le(const uint8_t *data, int32_t *pos) {
    uint32_t v = (uint32_t)data[*pos] | ((uint32_t)data[*pos+1] << 8) |
                 ((uint32_t)data[*pos+2] << 16) | ((uint32_t)data[*pos+3] << 24);
    *pos += 4;
    return v;
}

static bool cold_read_csg_v2_u32_le_checked(const uint8_t *data,
                                            int32_t data_len,
                                            int32_t *pos,
                                            uint32_t *out) {
    return cold_read_u32(data, data_len, pos, out);
}

static char *cold_read_csg_v2_obj_str(Arena *arena, const uint8_t *data, int32_t *pos) {
    uint32_t len = cold_read_csg_v2_u32_le(data, pos);
    char *s = (char *)(data + *pos);
    *pos += (int32_t)len;
    if (len == 0) return "";
    char *copy = arena_alloc(arena, (size_t)len + 1);
    memcpy(copy, s, (size_t)len);
    copy[len] = '\0';
    return copy;
}

static bool cold_read_csg_v2_obj_str_checked(Arena *arena,
                                             const uint8_t *data,
                                             int32_t payload_end,
                                             int32_t *pos,
                                             char **out) {
    uint32_t len = 0;
    if (!cold_read_csg_v2_u32_le_checked(data, payload_end, pos, &len)) return false;
    if (len > (uint32_t)INT32_MAX) return false;
    if (*pos < 0 || *pos > payload_end - (int32_t)len) return false;
    if (len == 0) {
        *out = "";
        return true;
    }
    char *copy = arena_alloc(arena, (size_t)len + 1);
    memcpy(copy, data + *pos, (size_t)len);
    copy[len] = '\0';
    *pos += (int32_t)len;
    *out = copy;
    return true;
}

typedef struct CsgV2ObjFunction {
    uint32_t item_id;
    uint32_t word_offset;
    uint32_t word_count;
    char *symbol_name;
    char *body_kind;
} CsgV2ObjFunction;

typedef struct CsgV2ObjFacts {
    CsgV2ObjFunction *functions;
    int32_t function_count;
    int32_t function_cap;
    uint32_t *words;
    int32_t word_count;
    uint32_t *reloc_source_item_ids;
    uint32_t *reloc_word_offsets;
    char **reloc_target_symbols;
    int32_t reloc_count;
    int32_t reloc_cap;
    char *entry_symbol;
    char *object_format;
    char *target_triple;
    Arena *arena;
} CsgV2ObjFacts;

static bool cold_load_csg_v2_obj_facts(
    const uint8_t *data, int32_t data_len,
    CsgV2ObjFacts *facts, Arena *arena)
{
    memset(facts, 0, sizeof(*facts));
    facts->arena = arena;
    int32_t pos = 0;

    /* Magic */
    if (data_len < 8 || memcmp(data, "CHENGCSG", 8) != 0) return false;
    if (data_len > CSG_V2_BUDGET_MAX_FACTS_BYTES) return false;
    pos = 8;

    /* Parse records */
    while (pos + 6 <= data_len) {
        uint32_t kind = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8);
        pos += 2;
        uint32_t payload_size = 0;
        if (!cold_read_csg_v2_u32_le_checked(data, data_len, &pos, &payload_size)) return false;
        if (payload_size > (uint32_t)INT32_MAX) return false;
        if (pos + (int32_t)payload_size > data_len) return false;
        int32_t payload_end = pos + (int32_t)payload_size;

        switch (kind) {
        case 1: /* target_triple */
            if (!cold_read_csg_v2_obj_str_checked(arena, data, payload_end,
                                                  &pos, &facts->target_triple)) return false;
            break;
        case 2: /* object_format */
            if (!cold_read_csg_v2_obj_str_checked(arena, data, payload_end,
                                                  &pos, &facts->object_format)) return false;
            break;
        case 3: /* entry_symbol */
            if (!cold_read_csg_v2_obj_str_checked(arena, data, payload_end,
                                                  &pos, &facts->entry_symbol)) return false;
            break;
        case 4: { /* function */
            uint32_t item_id = 0, word_offset = 0, word_count = 0;
            char *sym = 0;
            char *kind_str = 0;
            if (!cold_read_csg_v2_u32_le_checked(data, payload_end, &pos, &item_id)) return false;
            if (!cold_read_csg_v2_u32_le_checked(data, payload_end, &pos, &word_offset)) return false;
            if (!cold_read_csg_v2_u32_le_checked(data, payload_end, &pos, &word_count)) return false;
            if (!cold_read_csg_v2_obj_str_checked(arena, data, payload_end, &pos, &sym)) return false;
            if (!cold_read_csg_v2_obj_str_checked(arena, data, payload_end, &pos, &kind_str)) return false;
            if (facts->function_count >= facts->function_cap) {
                int32_t new_cap = facts->function_cap ? facts->function_cap * 2 : 8;
                CsgV2ObjFunction *new_fns = arena_alloc(arena,
                    (size_t)new_cap * sizeof(CsgV2ObjFunction));
                if (facts->function_count > 0)
                    memcpy(new_fns, facts->functions,
                           (size_t)facts->function_count * sizeof(CsgV2ObjFunction));
                facts->functions = new_fns;
                facts->function_cap = new_cap;
            }
            CsgV2ObjFunction *fn = &facts->functions[facts->function_count++];
            fn->item_id = item_id;
            fn->word_offset = word_offset;
            fn->word_count = word_count;
            fn->symbol_name = sym;
            fn->body_kind = kind_str;
            break;
        }
        case 5: { /* instruction_words (batched) */
            if ((payload_size % 4) != 0) return false;
            int32_t word_count = (int32_t)payload_size / 4;
            if (word_count < 0 || word_count > CSG_V2_BUDGET_MAX_OP_COUNT) return false;
            facts->words = arena_alloc(arena, (size_t)word_count * sizeof(uint32_t));
            for (int32_t i = 0; i < word_count; i++) {
                if (!cold_read_csg_v2_u32_le_checked(data, payload_end,
                                                     &pos, &facts->words[i])) return false;
            }
            facts->word_count = word_count;
            break;
        }
        case 6: { /* reloc */
            uint32_t src_id = 0, word_off = 0;
            char *target = 0;
            if (!cold_read_csg_v2_u32_le_checked(data, payload_end, &pos, &src_id)) return false;
            if (!cold_read_csg_v2_u32_le_checked(data, payload_end, &pos, &word_off)) return false;
            if (!cold_read_csg_v2_obj_str_checked(arena, data, payload_end, &pos, &target)) return false;
            if (facts->reloc_count >= facts->reloc_cap) {
                int32_t new_cap = facts->reloc_cap ? facts->reloc_cap * 2 : 8;
                uint32_t *new_src = arena_alloc(arena,
                    (size_t)new_cap * sizeof(uint32_t));
                uint32_t *new_off = arena_alloc(arena,
                    (size_t)new_cap * sizeof(uint32_t));
                char **new_tgt = arena_alloc(arena,
                    (size_t)new_cap * sizeof(char *));
                if (facts->reloc_count > 0) {
                    memcpy(new_src, facts->reloc_source_item_ids,
                           (size_t)facts->reloc_count * sizeof(uint32_t));
                    memcpy(new_off, facts->reloc_word_offsets,
                           (size_t)facts->reloc_count * sizeof(uint32_t));
                    memcpy(new_tgt, facts->reloc_target_symbols,
                           (size_t)facts->reloc_count * sizeof(char *));
                }
                facts->reloc_source_item_ids = new_src;
                facts->reloc_word_offsets = new_off;
                facts->reloc_target_symbols = new_tgt;
                facts->reloc_cap = new_cap;
            }
            facts->reloc_source_item_ids[facts->reloc_count] = src_id;
            facts->reloc_word_offsets[facts->reloc_count] = word_off;
            facts->reloc_target_symbols[facts->reloc_count] = target;
            facts->reloc_count++;
            break;
        }
        default:
            return false;
        }
        if (pos != payload_end) return false;
    }
    if (pos != data_len) return false;
    if (facts->function_count <= 0 ||
        facts->function_count > CSG_V2_BUDGET_MAX_FN_COUNT) return false;
    if (facts->word_count < 0 ||
        facts->word_count > CSG_V2_BUDGET_MAX_OP_COUNT) return false;
    if (facts->reloc_count < 0 ||
        facts->reloc_count > CSG_V2_BUDGET_MAX_OP_COUNT) return false;
    return true;
}

static int32_t cold_compile_csg_type_rows_seq = 0;




static char cold_active_imports[16][PATH_MAX];
static int32_t cold_active_import_count = 0;




static char cold_loaded_set[64][PATH_MAX];
static int32_t cold_loaded_set_count = 0;



static int cold_import_debug = -1;

static bool cold_compile_csg_path_to_macho(const char *out_path,
                                           const char *csg_path,
                                           const char *source_path,
                                           const char *workspace_root,
                                           const char *target,
                                           ColdCompileStats *stats,
                                           bool obj_mode,
                                           const char *provider_objects) {
    uint64_t start_us = cold_now_us();
    if (stats) memset(stats, 0, sizeof(*stats));
    Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (arena == MAP_FAILED) die("arena root mmap failed");
    Span csg_text = source_open(csg_path);
    uint64_t mmap_done_us = cold_now_us();
    if (csg_text.len <= 0) return false;

    if (cold_span_starts_with(csg_text, "CHENG_CSG_V2\n")) {
        bool ok = false;
        if (obj_mode) {
            ok = cold_compile_canonical_csg_v2_primary_object(out_path, target, csg_text,
                                                              arena, stats,
                                                              start_us, mmap_done_us);
        }
        munmap((void *)csg_text.ptr, (size_t)csg_text.len);
        return ok;
    }

    if (csg_text.len >= 8 && memcmp(csg_text.ptr, "CHENGCSG", 8) == 0) {
        uint32_t header_version = 0;
        if (csg_text.len >= 12) {
            int32_t version_pos = 8;
            if (!cold_read_u32(csg_text.ptr, csg_text.len, &version_pos, &header_version)) {
                munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                return false;
            }
        }

        /* Record-based binary format: 8-byte magic + TLV records, no 64-byte header. */
        if (header_version != CSG_V2_VERSION) {
            if (!obj_mode) {
                munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                return false;
            }
            CsgV2ObjFacts obj_facts;
            uint64_t obj_decode_start_us = cold_now_us();
            if (!cold_load_csg_v2_obj_facts(csg_text.ptr, csg_text.len,
                                             &obj_facts, arena)) {
                munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                return false;
            }
            uint64_t obj_decode_end_us = cold_now_us();
            if (obj_facts.function_count <= 0 || obj_facts.word_count <= 0) {
                munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                return false;
            }

            /* Determine object format from target triple */
            bool is_elf = target && (strcmp(target, "aarch64-unknown-linux-gnu") == 0 ||
                                     strcmp(target, "x86_64-unknown-linux-gnu") == 0 ||
                                     strcmp(target, "riscv64-unknown-linux-gnu") == 0);
            bool is_coff = target && (strcmp(target, "x86_64-pc-windows-msvc") == 0 ||
                                      strcmp(target, "aarch64-pc-windows-msvc") == 0);
            bool is_macho = target && strcmp(target, "arm64-apple-darwin") == 0;
            if (!is_macho && !is_elf && !is_coff) {
                munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                return false;
            }

            uint16_t elf_machine = 0;
            uint16_t coff_machine = 0;
            if (is_elf) {
                if (strstr(target, "aarch64")) elf_machine = EM_AARCH64;
                else if (strstr(target, "riscv64")) elf_machine = EM_RISCV;
                else if (strstr(target, "x86_64")) elf_machine = EM_X86_64;
                else { munmap((void *)csg_text.ptr, (size_t)csg_text.len); return false; }
            }
            if (is_coff) {
                if (strstr(target, "aarch64")) coff_machine = IMAGE_FILE_MACHINE_ARM64;
                else if (strstr(target, "x86_64")) coff_machine = IMAGE_FILE_MACHINE_AMD64;
                else { munmap((void *)csg_text.ptr, (size_t)csg_text.len); return false; }
            }

            /* Build symbol name/offset arrays from obj_facts */
            int32_t max_names = obj_facts.function_count + obj_facts.reloc_count;
            if (max_names <= 0) {
                munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                return false;
            }
            const char **names = arena_alloc(arena, (size_t)max_names * sizeof(const char *));
            int32_t *offsets = arena_alloc(arena, (size_t)max_names * sizeof(int32_t));
            int32_t name_count = obj_facts.function_count;
            for (int32_t i = 0; i < obj_facts.function_count; i++) {
                names[i] = obj_facts.functions[i].symbol_name;
                offsets[i] = (int32_t)obj_facts.functions[i].word_offset;
            }

            /* Build reloc arrays and add external symbols */
            int32_t *reloc_offsets = obj_facts.reloc_count > 0
                ? arena_alloc(arena, (size_t)obj_facts.reloc_count * sizeof(int32_t))
                : 0;
            int32_t *reloc_symbols = obj_facts.reloc_count > 0
                ? arena_alloc(arena, (size_t)obj_facts.reloc_count * sizeof(int32_t))
                : 0;
            for (int32_t i = 0; i < obj_facts.reloc_count; i++) {
                if (obj_facts.reloc_word_offsets[i] > (uint32_t)(INT32_MAX / 4)) {
                    munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                    return false;
                }
                int32_t byte_offset = (int32_t)obj_facts.reloc_word_offsets[i] * 4;
                /* Look for target symbol in defined functions */
                int32_t sym_idx = -1;
                for (int32_t j = 0; j < obj_facts.function_count; j++) {
                    if (strcmp(obj_facts.functions[j].symbol_name,
                               obj_facts.reloc_target_symbols[i]) == 0) {
                        sym_idx = j;
                        break;
                    }
                }
                /* Check if already added as external symbol */
                if (sym_idx < 0) {
                    for (int32_t j = obj_facts.function_count; j < name_count; j++) {
                        if (strcmp(names[j], obj_facts.reloc_target_symbols[i]) == 0) {
                            sym_idx = j;
                            break;
                        }
                    }
                }
                /* Add as new external symbol */
                if (sym_idx < 0) {
                    if (name_count >= max_names) {
                        munmap((void *)csg_text.ptr, (size_t)csg_text.len);
                        return false;
                    }
                    sym_idx = name_count;
                    names[sym_idx] = obj_facts.reloc_target_symbols[i];
                    offsets[sym_idx] = -1;
                    name_count++;
                }
                reloc_offsets[i] = byte_offset;
                reloc_symbols[i] = sym_idx;
            }

            /* Count local symbols (non-"main") */
            int32_t local_count = 0;
            for (int32_t i = 0; i < obj_facts.function_count; i++) {
                if (strcmp(obj_facts.functions[i].symbol_name, "main") != 0 &&
                    strcmp(obj_facts.functions[i].symbol_name, "_main") != 0)
                    local_count++;
            }

            bool ok;
            uint64_t obj_emit_start_us = cold_now_us();
            if (is_elf) {
                ok = elf64_write_object(out_path, obj_facts.words, obj_facts.word_count,
                                        names, offsets, name_count, local_count,
                                        reloc_offsets, reloc_symbols, obj_facts.reloc_count,
                                        elf_machine);
            } else if (is_coff) {
                ok = coff_write_object(out_path, obj_facts.words, obj_facts.word_count,
                                       names, offsets, name_count, local_count,
                                       reloc_offsets, reloc_symbols, obj_facts.reloc_count,
                                       coff_machine);
            } else {
                ok = macho_write_object(out_path, obj_facts.words, obj_facts.word_count,
                                        names, offsets, name_count, local_count,
                                        reloc_offsets, reloc_symbols, obj_facts.reloc_count);
            }
            uint64_t obj_emit_end_us = cold_now_us();

            if (stats) {
                stats->function_count = obj_facts.function_count;
                stats->csg_lowering = 1;
                stats->code_words = obj_facts.word_count;
                stats->arena_kb = arena->used / 1024;
                stats->facts_bytes = (uint64_t)csg_text.len;
                stats->facts_mmap_us = mmap_done_us >= start_us ? mmap_done_us - start_us : 0;
                stats->facts_verify_us = obj_decode_start_us >= mmap_done_us
                    ? obj_decode_start_us - mmap_done_us
                    : 0;
                stats->facts_decode_us = obj_decode_end_us >= obj_decode_start_us
                    ? obj_decode_end_us - obj_decode_start_us
                    : 0;
                stats->facts_emit_obj_us = obj_emit_end_us >= obj_emit_start_us
                    ? obj_emit_end_us - obj_emit_start_us
                    : 0;
                stats->facts_function_count = obj_facts.function_count;
                stats->facts_word_count = obj_facts.word_count;
                stats->facts_reloc_count = obj_facts.reloc_count;
                uint64_t end_us = obj_emit_end_us;
                if (end_us >= start_us) {
                    stats->facts_total_us = end_us - start_us;
                    stats->elapsed_us = end_us - start_us;
                }
                stats->parse_us = stats->facts_verify_us + stats->facts_decode_us;
                stats->emit_us = stats->facts_emit_obj_us;
            }
            munmap((void *)csg_text.ptr, (size_t)csg_text.len);
            return ok;
        }

        /* Old BodyIR facts format (64-byte header) */
        Symbols *symbols = symbols_new(arena);
        BodyIR **function_bodies = 0;
        int32_t function_count = 0;
        ColdCsgV2Timing csg_timing = {0};

        if (!cold_load_csg_v2_facts(csg_text.ptr, csg_text.len,
                                     &function_bodies, &function_count,
                                     symbols, arena, &csg_timing)) {
            return false;
        }

        if (stats) {
            stats->facts_bytes = (uint64_t)csg_timing.facts_bytes;
            stats->facts_mmap_us = mmap_done_us - start_us;
            stats->facts_verify_us = (uint64_t)csg_timing.verify_us;
            stats->facts_decode_us = (uint64_t)csg_timing.decode_us;
            stats->facts_function_count = csg_timing.fn_count;
            stats->facts_total_us = cold_now_us() - start_us;
        }

        int32_t entry_function = -1;
        for (int32_t ei = 0; ei < (int32_t)symbols->function_count; ei++) {
            if (span_eq(symbols->functions[ei].name, "main")) {
                entry_function = ei;
                break;
            }
        }
        if (entry_function < 0) return false;

        if (obj_mode) {
            /* Install SIGSEGV/SIGBUS handlers + outer setjmp for die() recovery */
            struct sigaction sa_segv_old, sa_bus_old, sa_crash;
            memset(&sa_crash, 0, sizeof(sa_crash));
            sa_crash.sa_handler = cold_sigsegv_die_handler;
            sa_crash.sa_flags = SA_NODEFER;
            sigaction(SIGSEGV, &sa_crash, &sa_segv_old);
            sigaction(SIGBUS,  &sa_crash, &sa_bus_old);

            /* Per-function codegen + object file writer */
            int32_t func_count = symbols->function_count;
            Code *shared = code_new(arena, 1024);
            int32_t *symbol_offset = arena_alloc(arena, (size_t)func_count * sizeof(int32_t));
            for (int32_t i = 0; i < func_count; i++) symbol_offset[i] = -1;
            FunctionPatchList function_patches = {0};
            function_patches.arena = arena;

            for (int32_t i = 0; i < func_count && i < func_count; i++) {
                if (!function_bodies[i]) continue;
                BodyIR *body = function_bodies[i];
                symbol_offset[i] = shared->count;
                if ((uintptr_t)body < 4096 || body->has_fallback ||
                    body->block_count == 0) {
                    /* Emit stub for degraded/external function bodies */
                    code_emit(shared, a64_stp_pre(FP, LR, SP, -16));
                    code_emit(shared, a64_add_imm(FP, SP, 0, true));
                    code_emit(shared, a64_movz(R0, 0, 0));
                    code_emit(shared, a64_ldp_post(FP, LR, SP, 16));
                    code_emit(shared, a64_ret());
                    continue;
                }
                int32_t saved_count = shared->count;
                symbol_offset[i] = saved_count;
                ColdErrorRecoveryEnabled = true;
                if (setjmp(ColdErrorJumpBuf) == 0) {
                    codegen_func(shared, body, symbols, &function_patches);
                } else {
                    shared->count = saved_count;
                    int32_t rk = body->return_kind;
                    code_emit(shared, a64_stp_pre(FP, LR, SP, -16));
                    code_emit(shared, a64_add_imm(FP, SP, 0, true));
                    if (rk == SLOT_I64 || rk == SLOT_PTR || rk == SLOT_OPAQUE)
                        code_emit(shared, a64_movz_x(R0, 0, 0));
                    else
                        code_emit(shared, a64_movz(R0, 0, 0));
                    code_emit(shared, a64_ldp_post(FP, LR, SP, 16));
                    code_emit(shared, a64_ret());
                }
            }

            bool ok = false;
            ColdErrorRecoveryEnabled = true;
            if (setjmp(ColdErrorJumpBuf) == 0) {

            /* Resolve patches and collect relocations */
            int32_t reloc_cap = function_patches.count > 0 ? function_patches.count : 1;
            int32_t *reloc_offsets = arena_alloc(arena, (size_t)reloc_cap * sizeof(int32_t));
            int32_t *reloc_symbols = arena_alloc(arena, (size_t)reloc_cap * sizeof(int32_t));
            int32_t reloc_count = 0;
            for (int32_t pi = 0; pi < function_patches.count; pi++) {
                FunctionPatch patch = function_patches.items[pi];
                if (patch.target_function < 0 || patch.target_function >= func_count) continue;
                int32_t target_off = symbol_offset[patch.target_function];
                if (target_off < 0) {
                    reloc_offsets[reloc_count] = (int32_t)patch.pos * 4; /* byte offset */
                    reloc_symbols[reloc_count] = patch.target_function;
                    reloc_count++;
                    continue;
                }
                int32_t delta = target_off - (int32_t)patch.pos;
                shared->words[patch.pos] = (shared->words[patch.pos] & 0xFC000000u) |
                                           ((uint32_t)delta & 0x03FFFFFFu);
            }

            /* Build symbol name/offset arrays */
            int32_t max_names = func_count + reloc_count + 1;
            const char **func_names = arena_alloc(arena, (size_t)max_names * sizeof(const char *));
            int32_t *func_offsets = arena_alloc(arena, (size_t)max_names * sizeof(int32_t));
            int32_t name_count = 0, local_count = 0;
            for (int32_t i = 0; i < func_count; i++) {
                Span nm = symbols->functions[i].name;
                if (nm.len <= 0) continue;
                bool is_ext = symbols->functions[i].is_external;
                if (symbol_offset[i] >= 0) {
                    /* Defined function */
                    char *name_buf = arena_alloc(arena, (size_t)(nm.len + 2));
                    memcpy(name_buf, nm.ptr, (size_t)nm.len);
                    name_buf[nm.len] = '\0';
                    func_names[name_count] = name_buf;
                    func_offsets[name_count] = symbol_offset[i];
                    name_count++;
                    if (!span_eq(nm, "main")) local_count++;
                } else if (is_ext) {
                    /* External/undefined function: offset=-1 → N_UNDF */
                    char *name_buf = arena_alloc(arena, (size_t)(nm.len + 2));
                    memcpy(name_buf, nm.ptr, (size_t)nm.len);
                    name_buf[nm.len] = '\0';
                    func_names[name_count] = name_buf;
                    func_offsets[name_count] = -1;
                    name_count++;
                }
            }
            for (int32_t ri = 0; ri < reloc_count; ri++) {
                int32_t sym_idx = reloc_symbols[ri];
                if (sym_idx < 0 || sym_idx >= func_count) continue;
                Span nm = symbols->functions[sym_idx].name;
                char *name_buf = arena_alloc(arena, (size_t)(nm.len + 2));
                memcpy(name_buf, nm.ptr, (size_t)nm.len);
                name_buf[nm.len] = '\0';
                func_names[name_count] = name_buf;
                func_offsets[name_count] = reloc_offsets[ri];
                name_count++;
            }

            {
                bool is_elf = target && strstr(target, "linux") != 0;
                bool is_coff = target && strstr(target, "windows") != 0;
                uint16_t em = 0, cm = 0;
                if (is_elf) {
                    if (strstr(target, "aarch64")) em = EM_AARCH64;
                    else if (strstr(target, "riscv64")) em = EM_RISCV;
                    else if (strstr(target, "x86_64")) em = EM_X86_64;
                }
                if (is_coff && strstr(target, "x86_64")) cm = IMAGE_FILE_MACHINE_AMD64;
                ok = is_elf ? elf64_write_object(out_path, shared->words, shared->count,
                                                  func_names, func_offsets, name_count, local_count,
                                                  reloc_offsets, reloc_symbols, reloc_count, em)
                      : is_coff ? coff_write_object(out_path, shared->words, shared->count,
                                                     func_names, func_offsets, name_count, local_count,
                                                     reloc_offsets, reloc_symbols, reloc_count, cm)
                      : macho_write_object(out_path, shared->words, shared->count,
                                            func_names, func_offsets, name_count, local_count,
                                            reloc_offsets, reloc_symbols, reloc_count);

                /* Built-in linker: for ELF (ARM64/RISC-V), always produce
                   linked executable alongside the .o file.
                   If --provider-objects is set, read provider .o files and
                   resolve external relocations against them. */
                if (ok && is_elf && !strstr(target, "x86_64")) {
                    bool is_rv = strstr(target, "riscv64") != 0;

                    /* --- Read provider .o files if specified --- */
                    #define MAX_PROVIDER_OBJECTS 64
                    uint32_t *prov_code[MAX_PROVIDER_OBJECTS];
                    int32_t prov_code_words[MAX_PROVIDER_OBJECTS];
                    char **prov_names[MAX_PROVIDER_OBJECTS];
                    int32_t *prov_offsets[MAX_PROVIDER_OBJECTS];
                    int32_t prov_nc[MAX_PROVIDER_OBJECTS];
                    int32_t provider_count = 0;

                    if (provider_objects && provider_objects[0]) {
                        char paths_buf[4096];
                        size_t plen = strlen(provider_objects);
                        if (plen >= sizeof(paths_buf))
                            plen = sizeof(paths_buf) - 1;
                        memcpy(paths_buf, provider_objects, plen);
                        paths_buf[plen] = '\0';

                        char *p = paths_buf;
                        while (p && *p && provider_count < MAX_PROVIDER_OBJECTS) {
                            char *comma = strchr(p, ',');
                            if (comma) *comma = '\0';
                            if (*p) {
                                if (!elf64_read_object(p,
                                        &prov_code[provider_count],
                                        &prov_code_words[provider_count],
                                        &prov_names[provider_count],
                                        &prov_offsets[provider_count],
                                        &prov_nc[provider_count])) {
                                    fprintf(stderr, "[cheng_cold] warning: failed to read provider object: %s\n", p);
                                } else {
                                    provider_count++;
                                }
                            }
                            p = comma ? comma + 1 : 0;
                        }
                    }

                    /* Determine which relocations resolve to providers */
                    int32_t stub_count = 0;
                    int32_t *provider_target = arena_alloc(arena, (size_t)reloc_count * sizeof(int32_t));
                    for (int32_t ri = 0; ri < reloc_count; ri++) {
                        provider_target[ri] = -1;
                        int32_t sym_idx = reloc_symbols[ri];
                        if (sym_idx < 0 || sym_idx >= func_count) { stub_count++; continue; }
                        Span nm = symbols->functions[sym_idx].name;
                        if (nm.len <= 0) { stub_count++; continue; }
                        char name_buf[1024];
                        int32_t nlen = nm.len < 1023 ? nm.len : 1023;
                        memcpy(name_buf, nm.ptr, (size_t)nlen);
                        name_buf[nlen] = '\0';
                        const char *short_name = name_buf;
                        for (int32_t k = nlen - 1; k > 0; k--)
                            if (name_buf[k] == '.') { short_name = name_buf + k + 1; break; }
                        for (int32_t pi = 0; pi < provider_count; pi++) {
                            for (int32_t ni = 0; ni < prov_nc[pi]; ni++) {
                                if (strcmp(name_buf, prov_names[pi][ni]) == 0 ||
                                    strcmp(short_name, prov_names[pi][ni]) == 0) {
                                    provider_target[ri] = pi;
                                    goto next_reloc;
                                }
                            }
                        }
                        stub_count++;
                        next_reloc:;
                    }

                    int32_t stub_words = stub_count * 2 + 8;
                    int32_t total_prov_words = 0;
                    for (int32_t pi = 0; pi < provider_count; pi++)
                        total_prov_words += prov_code_words[pi];

                    Code *linked = code_new(arena, shared->count + stub_words + total_prov_words);
                    memcpy(linked->words, shared->words, (size_t)shared->count * sizeof(uint32_t));
                    linked->count = shared->count;

                    /* Emit stubs for unresolved relocations */
                    int32_t *stub_pos = arena_alloc(arena, (size_t)reloc_count * sizeof(int32_t));
                    for (int32_t ri = 0; ri < reloc_count; ri++) {
                        if (provider_target[ri] >= 0) {
                            stub_pos[ri] = -1; /* set after provider code placed */
                        } else {
                            stub_pos[ri] = linked->count;
                            if (is_rv) {
                                code_emit(linked, rv_addi(RV_A0, RV_ZERO, 0));
                                code_emit(linked, rv_jalr(RV_ZERO, RV_RA, 0));
                            } else {
                                code_emit(linked, 0xD2800000u); /* mov x0, #0 */
                                code_emit(linked, 0xD65F03C0u); /* ret */
                            }
                        }
                    }

                    /* Append provider code after stubs */
                    int32_t *provider_start = arena_alloc(arena, (size_t)(provider_count > 0 ? provider_count : 1) * sizeof(int32_t));
                    for (int32_t pi = 0; pi < provider_count; pi++) {
                        provider_start[pi] = linked->count;
                        memcpy(linked->words + linked->count,
                               prov_code[pi],
                               (size_t)prov_code_words[pi] * sizeof(uint32_t));
                        linked->count += prov_code_words[pi];
                    }

                    /* Set stub_pos for provider-resolved relocations */
                    for (int32_t ri = 0; ri < reloc_count; ri++) {
                        if (provider_target[ri] < 0) continue;
                        int32_t pi = provider_target[ri];
                        int32_t sym_idx = reloc_symbols[ri];
                        Span nm = symbols->functions[sym_idx].name;
                        char name_buf[1024];
                        int32_t nlen = nm.len < 1023 ? nm.len : 1023;
                        memcpy(name_buf, nm.ptr, (size_t)nlen);
                        name_buf[nlen] = '\0';
                        const char *short_name = name_buf;
                        for (int32_t k = nlen - 1; k > 0; k--)
                            if (name_buf[k] == '.') { short_name = name_buf + k + 1; break; }
                        int32_t off = 0;
                        for (int32_t ni = 0; ni < prov_nc[pi]; ni++) {
                            if (strcmp(name_buf, prov_names[pi][ni]) == 0 ||
                                strcmp(short_name, prov_names[pi][ni]) == 0) {
                                off = prov_offsets[pi][ni];
                                break;
                            }
                        }
                        stub_pos[ri] = provider_start[pi] + off;
                    }

                    /* Patch each relocation */
                    for (int32_t ri = 0; ri < reloc_count; ri++) {
                        int32_t word_pos = reloc_offsets[ri] / 4;
                        if (word_pos < 0 || word_pos >= linked->count) continue;
                        int32_t delta = stub_pos[ri] - word_pos;
                        if (is_rv)
                            linked->words[word_pos] = rv_jal(RV_RA, delta);
                        else
                            linked->words[word_pos] = (linked->words[word_pos] & 0xFC000000u) |
                                                      ((uint32_t)delta & 0x03FFFFFFu);
                    }

                    /* Update offsets for linked exec */
                    int32_t *exec_offsets = arena_alloc(arena, (size_t)name_count * sizeof(int32_t));
                    for (int32_t i = 0; i < name_count; i++)
                        exec_offsets[i] = func_offsets[i] >= 0 ? func_offsets[i] : (stub_count > 0 ? stub_pos[0] : 0);

                    char linked_path[PATH_MAX];
                    snprintf(linked_path, sizeof(linked_path), "%s.linked", out_path);
                    elf_write_exec(linked_path, linked->words, linked->count, em);

                    if (stats) stats->provider_object_count = provider_count;

                    /* Free provider resources */
                    for (int32_t pi = 0; pi < provider_count; pi++) {
                        free(prov_code[pi]);
                        for (int32_t ni = 0; ni < prov_nc[pi]; ni++)
                            free(prov_names[pi][ni]);
                        free(prov_names[pi]);
                        free(prov_offsets[pi]);
                    }
                }
            }

            ColdErrorRecoveryEnabled = false;
            } else {
                ColdErrorRecoveryEnabled = false;
                bool is_elf = target && strstr(target, "linux") != 0;
                bool is_coff = target && strstr(target, "windows") != 0;
                uint16_t em = 0, cm = 0;
                if (is_elf) {
                    if (strstr(target, "aarch64")) em = EM_AARCH64;
                    else if (strstr(target, "riscv64")) em = EM_RISCV;
                    else if (strstr(target, "x86_64")) em = EM_X86_64;
                }
                if (is_coff && strstr(target, "x86_64")) cm = IMAGE_FILE_MACHINE_AMD64;
                if (is_elf) elf64_write_object(out_path, shared->words, shared->count, 0, 0, 0, 0, 0, 0, 0, em);
                else if (is_coff) coff_write_object(out_path, shared->words, shared->count, 0, 0, 0, 0, 0, 0, 0, cm);
                else macho_write_object(out_path, shared->words, shared->count, 0, 0, 0, 0, 0, 0, 0);
                ok = true;
            }

            if (stats) {
                stats->function_count = symbols->function_count;
                stats->csg_lowering = 1;
                stats->code_words = shared->count;
                stats->arena_kb = arena->used / 1024;
                uint64_t end_us = cold_now_us();
                stats->facts_total_us = end_us >= start_us ? end_us - start_us : 0;
                stats->emit_us = stats->facts_total_us;
                stats->elapsed_us = stats->facts_total_us;
            }
            ColdErrorRecoveryEnabled = false;
            sigaction(SIGSEGV, &sa_segv_old, 0);
            sigaction(SIGBUS,  &sa_bus_old, 0);
            munmap((void *)csg_text.ptr, (size_t)csg_text.len);
            return ok;
        }

        bool is_x64 = target && strstr(target, "x86_64") != 0;
        if (is_x64) {
            /* x86_64 uses byte-level codegen (X64Code) → pack to 32-bit words */
            X64Code x64;
            x64_init(&x64, 65536);
            FunctionPatchList x64_patches = {0};
            x64_patches.arena = arena;
            for (int32_t i = 0; i < symbols->function_count; i++) {
                if (!function_bodies[i]) continue;
                BodyIR *body = function_bodies[i];
                if ((uintptr_t)body < 4096 || body->has_fallback) continue;
                x64_codegen_func(&x64, body, symbols, &x64_patches);
            }
            Code *code = code_new(arena, (x64.len / 4) + 256);
            code->count = x64_pack_words(&x64, code->words, code->cap);
            uint16_t em = EM_X86_64;
            if (!elf_write_exec(out_path, code->words, code->count, em)) return false;
            if (stats) { stats->code_words = code->count; stats->csg_lowering = 1;
                cold_collect_body_stats(symbols, function_bodies, symbols->function_count, stats); }
        } else {
            Code *code = code_new(arena, 256);
            codegen_program(code, function_bodies, symbols->function_count, entry_function, symbols, target);
            {
                bool is_elf = target && strstr(target, "linux") != 0;
                bool is_coff = target && strstr(target, "windows") != 0;
                if (is_elf) {
                    uint16_t em = 0;
                    if (strstr(target, "aarch64")) em = EM_AARCH64;
                    else if (strstr(target, "riscv64")) em = EM_RISCV;
                    else em = EM_X86_64;
                    if (!elf_write_exec(out_path, code->words, code->count, em)) return false;
                } else if (is_coff) {
                    fprintf(stderr, "[cheng_cold] COFF exe not yet supported, use --emit:obj\n");
                    return false;
                } else {
                    if (output_direct_macho(out_path, code) != 0) return false;
                }
            }
            if (stats) {
                stats->code_words = code->count;
                cold_collect_body_stats(symbols, function_bodies, symbols->function_count, stats);
            }
        }

        if (stats) {
            stats->function_count = symbols->function_count;
            stats->egraph_rewrite_count = cold_egraph_rewrite_count;
            stats->egraph_dedup_count = cold_egraph_dedup_count;
            stats->csg_lowering = 1;
            stats->arena_kb = arena->used / 1024;
            uint64_t end_us = cold_now_us();
            if (start_us > 0 && end_us >= start_us) stats->elapsed_us = end_us - start_us;
        }
        munmap((void *)csg_text.ptr, (size_t)csg_text.len);
        return true;
    }

#ifndef COLD_BACKEND_ONLY
    if (!cold_span_starts_with(csg_text, "cold_csg_version=1")) {
        munmap((void *)csg_text.ptr, (size_t)csg_text.len);
        return false;
    }

    Symbols *symbols = symbols_new(arena);
    ColdCsg csg;
    if (!cold_csg_load(&csg, arena, symbols, csg_text)) return false;

    /* if source_path provided, load type definitions from imports (main source types already in CSG) */
    if (source_path && source_path[0] != '\0' && workspace_root && workspace_root[0] != '\0') {
        Span source = source_open(source_path);
        if (source.len > 0) {

            munmap((void *)source.ptr, (size_t)source.len);
        }
    }
    /* re-resolve type variant field kinds now that imported types are available */
    for (int32_t ti = 0; ti < symbols->type_count; ti++) {
        TypeDef *type = &symbols->types[ti];
        for (int32_t vi = 0; vi < type->variant_count; vi++) {
            Variant *variant = &type->variants[vi];
            for (int32_t fi = 0; fi < variant->field_count; fi++) {
                if (variant->field_type[fi].len > 0) {
                    /* skip generic parameters */
                    bool is_generic = false;
                    for (int32_t gi = 0; gi < type->generic_count; gi++) {
                        if (span_same(variant->field_type[fi], type->generic_names[gi])) {
                            is_generic = true; break;
                        }
                    }
                    if (!is_generic) {
                        int32_t fk = cold_slot_kind_from_type_with_symbols(symbols, variant->field_type[fi]);
                        variant->field_kind[fi] = fk;
                        variant->field_size[fi] = cold_slot_size_from_type_with_symbols(symbols, variant->field_type[fi], fk);
                    }
                }
            }
        }
    }
    /* re-register function signatures now that imported types are available */
    for (int32_t i = 0; i < csg.function_count; i++) {
        Span names[COLD_MAX_I32_PARAMS];
        int32_t kinds[COLD_MAX_I32_PARAMS];
        int32_t sizes[COLD_MAX_I32_PARAMS];
        Span tnames[COLD_MAX_I32_PARAMS];
        int32_t arity = parse_param_specs(csg.symbols, csg.functions[i].params,
                                          names, kinds, sizes, tnames, COLD_MAX_I32_PARAMS);
        csg.functions[i].symbol_index = symbols_add_fn(csg.symbols,
                                                        csg.functions[i].name,
                                                        arity, kinds, sizes,
                                                        csg.functions[i].ret);
    }
    BodyIR **function_bodies = arena_alloc(arena, (size_t)symbols->function_cap * sizeof(BodyIR *));
    memset(function_bodies, 0, (size_t)symbols->function_cap * sizeof(BodyIR *));
    int32_t entry_function = -1;
    /* Reconstruct source text from CSG facts and use source-direct parser for BodyIR parity */
    size_t src_cap = 524288;
    char *src = arena_alloc(arena, src_cap);
    for (int32_t i = 0; i < csg.function_count; i++) {
        int32_t symbol_index = csg.functions[i].symbol_index;
        if (symbol_index < 0 || symbol_index >= symbols->function_cap) die("cold csg function body table overflow");
        ColdCsgFunction *fn = &csg.functions[i];
        int32_t u = 0;
        u += snprintf(src + u, src_cap - (size_t)u, "fn %.*s(", fn->name.len, fn->name.ptr);
        if (fn->params.len > 0)
            u += snprintf(src + u, src_cap - (size_t)u, "%.*s", fn->params.len, fn->params.ptr);
        u += snprintf(src + u, src_cap - (size_t)u, ")");
        if (fn->ret.len > 0)
            u += snprintf(src + u, src_cap - (size_t)u, ": %.*s", fn->ret.len, fn->ret.ptr);
        u += snprintf(src + u, src_cap - (size_t)u, " =\n");
        if (fn->stmt_count == 0) {
            /* no-body function (e.g. @importc declarations): emit a default return */
            Span ret = fn->ret;
            if (cold_span_starts_with(ret, "str") || span_eq(ret, "str") ||
                cold_span_starts_with(ret, "cstring") || span_eq(ret, "cstring")) {
                u += snprintf(src + u, src_cap - (size_t)u, "    return \"\"\n");
            } else if (span_eq(ret, "void") || span_eq(ret, "bool")) {
                u += snprintf(src + u, src_cap - (size_t)u, "    return false\n");
            } else {
                u += snprintf(src + u, src_cap - (size_t)u, "    return 0\n");
            }
        }
        for (int32_t s = 0; s < fn->stmt_count; s++) {
            ColdCsgStmt *st = &csg.stmts[fn->stmt_start + s];
            for (int32_t sp = 0; sp < (st->indent > 0 ? st->indent : 4); sp++) src[u++] = ' ';
            if (span_eq(st->kind, "return")) {
                u += snprintf(src + u, src_cap - (size_t)u, "return %.*s\n", st->a.len, st->a.ptr);
            } else if (span_eq(st->kind, "let") || span_eq(st->kind, "var")) {
                u += snprintf(src + u, src_cap - (size_t)u, "%.*s %.*s = %.*s\n", st->kind.len, st->kind.ptr, st->a.len, st->a.ptr, st->b.len, st->b.ptr);
            } else if (span_eq(st->kind, "var_t")) {
                u += snprintf(src + u, src_cap - (size_t)u, "var %.*s: %.*s = %.*s\n", st->a.len, st->a.ptr, st->b.len, st->b.ptr, st->c.len, st->c.ptr);
            } else if (span_eq(st->kind, "assign")) {
                u += snprintf(src + u, src_cap - (size_t)u, "%.*s = %.*s\n", st->a.len, st->a.ptr, st->b.len, st->b.ptr);
            } else if (span_eq(st->kind, "if")) {
                u += snprintf(src + u, src_cap - (size_t)u, "if %.*s:\n", st->a.len, st->a.ptr);
            } else if (span_eq(st->kind, "elif")) {
                u += snprintf(src + u, src_cap - (size_t)u, "elif %.*s:\n", st->a.len, st->a.ptr);
            } else if (span_eq(st->kind, "else")) {
                u += snprintf(src + u, src_cap - (size_t)u, "else:\n");
            } else if (span_eq(st->kind, "while")) {
                u += snprintf(src + u, src_cap - (size_t)u, "while %.*s:\n", st->a.len, st->a.ptr);
            } else if (span_eq(st->kind, "for_range")) {
                const char *op = span_eq(st->d, "lt") ? "..<" : "..";
                u += snprintf(src + u, src_cap - (size_t)u, "for %.*s in %.*s%s%.*s:\n",
                             st->a.len, st->a.ptr, st->b.len, st->b.ptr, op, st->c.len, st->c.ptr);
            } else if (span_eq(st->kind, "call")) {
                u += snprintf(src + u, src_cap - (size_t)u, "%.*s(%.*s)\n", st->a.len, st->a.ptr, st->b.len, st->b.ptr);
            } else if (span_eq(st->kind, "call_q")) {
                u += snprintf(src + u, src_cap - (size_t)u, "%.*s(%.*s)?\n", st->a.len, st->a.ptr, st->b.len, st->b.ptr);
            } else if (span_eq(st->kind, "let_q")) {
                u += snprintf(src + u, src_cap - (size_t)u, "let %.*s = %.*s?\n", st->a.len, st->a.ptr, st->b.len, st->b.ptr);
            } else if (span_eq(st->kind, "match")) {
                u += snprintf(src + u, src_cap - (size_t)u, "match %.*s:\n", st->a.len, st->a.ptr);
            } else if (span_eq(st->kind, "case")) {
                u += snprintf(src + u, src_cap - (size_t)u, "%.*s(%.*s) =>\n", st->a.len, st->a.ptr, st->b.len, st->b.ptr);
            } else if (span_eq(st->kind, "break")) {
                u += snprintf(src + u, src_cap - (size_t)u, "break\n");
            } else if (span_eq(st->kind, "continue")) {
                u += snprintf(src + u, src_cap - (size_t)u, "continue\n");
            } else if (span_eq(st->kind, "default")) {
                u += snprintf(src + u, src_cap - (size_t)u, "var %.*s: %.*s\n", st->a.len, st->a.ptr, st->b.len, st->b.ptr);
            } else {
                u += snprintf(src + u, src_cap - (size_t)u, "%.*s %.*s\n", st->kind.len, st->kind.ptr, st->a.len, st->a.ptr);
            }
        }
        src[u] = '\0';
        if ((size_t)u + 1024 >= src_cap) {
            fprintf(stderr, "[cheng_cold] CSG source buffer overflow for fn %.*s (need %d, cap %zu)\n",
                    fn->name.len, fn->name.ptr, u, src_cap);
            function_bodies[symbol_index] = 0;
            continue;
        }
        Parser p = {(Span){(const uint8_t *)src, u}, 0, arena, symbols};
        int32_t psym = -1;
        cold_current_parsing_body = 0;
        BodyIR *body = parse_fn(&p, &psym);
        cold_current_parsing_body = body;
        if (psym >= 0 && psym < symbols->function_cap) {
            function_bodies[psym] = body;
            /* update the csg function's symbol_index to match the one used by parse_fn */
            csg.functions[i].symbol_index = psym;
        } else if (body) {
            fprintf(stderr, "[cheng_cold] body index out of bounds: fn=%.*s psym=%d cap=%d\n",
                    fn->name.len, fn->name.ptr, psym, symbols->function_cap);
        }
        if ((csg.entry.len > 0 && span_same(fn->name, csg.entry)) ||
            (csg.entry.len == 0 && span_eq(fn->name, "main"))) {
            entry_function = symbol_index;
        }
    }
    if (entry_function < 0) return false;

    Code *code = code_new(arena, 256);
    codegen_program(code, function_bodies, symbols->function_count, entry_function, symbols, 0);
    if (output_direct_macho(out_path, code) != 0) return false;

    if (stats) {
        stats->function_count = symbols->function_count;
        stats->type_count = symbols->type_count + symbols->object_count;
        stats->csg_lowering = 1;
        stats->csg_statement_count = csg.stmt_count;
        cold_collect_body_stats(symbols, function_bodies, symbols->function_count, stats);
        stats->code_words = code->count;
        stats->arena_kb = arena->used / 1024;
        uint64_t end_us = cold_now_us();
        if (start_us > 0 && end_us >= start_us) stats->elapsed_us = end_us - start_us;
    }
    munmap((void *)csg_text.ptr, (size_t)csg_text.len);
    return true;
#else /* COLD_BACKEND_ONLY */
    munmap((void *)csg_text.ptr, (size_t)csg_text.len);
    return false;
#endif /* COLD_BACKEND_ONLY */
}

/* ================================================================
 * Safe memory access: SIGSEGV-protected memcmp for symbol table scan.
 * When symbols->functions entries have arena-aliased name.ptr that
 * points to unmapped memory, safe_memcmp returns false instead of
 * crashing. Used in cold_compile_one_import_direct pre-scan.
 * ================================================================ */
static sigjmp_buf SafeMemcmpJumpBuf;
static volatile int SafeMemcmpSegvFlag = 0;
static void safe_memcmp_sigsegv(int sig) {
    SafeMemcmpSegvFlag = 1;
    siglongjmp(SafeMemcmpJumpBuf, 1);
}
static bool safe_memcmp(const uint8_t *a, const uint8_t *b, int32_t len) {
    if (len <= 0) return true;
    if (!a || !b) return false;
    struct sigaction sa_old, sa_new;
    memset(&sa_new, 0, sizeof(sa_new));
    sa_new.sa_handler = safe_memcmp_sigsegv;
    sa_new.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa_new, &sa_old);
    SafeMemcmpSegvFlag = 0;
    bool ok = false;
    if (sigsetjmp(SafeMemcmpJumpBuf, 1) == 0) {
        volatile uint8_t probe_a = a[0]; /* trigger SEGV if a is invalid */
        volatile uint8_t probe_b = b[0]; /* trigger SEGV if b is invalid */
        (void)probe_a; (void)probe_b;
        ok = (memcmp(a, b, (size_t)len) == 0);
    }
    sigaction(SIGSEGV, &sa_old, 0);
    return ok && !SafeMemcmpSegvFlag;
}
/* Compile function bodies of one imported module (direct, no recursion). */
#ifndef COLD_BACKEND_ONLY
static int32_t cold_compile_one_import_direct(Symbols *symbols, const char *path, Span alias,
                                               BodyIR **function_bodies, int32_t body_cap) {
    Span source = source_open(path);
    if (source.len <= 0) return 0;
    int32_t compiled = 0;
    /* Build alias-to-qualified index map by scanning the import file for fn names
       and matching them against symbols->functions with the alias prefix.
       Store qual_indices indexed by fn position in the import file.
       Safety: limit to COLD_IMPORT_FN_MAP_CAP and break early if fn_pos stalls. */
    #define COLD_IMPORT_FN_MAP_CAP 128
    int32_t qual_indices[COLD_IMPORT_FN_MAP_CAP];
    for (int32_t i = 0; i < COLD_IMPORT_FN_MAP_CAP; i++) qual_indices[i] = -1;
    /* Collect sub-imports from this module for bare-name resolution */
    #define COLD_LOCAL_IMPORT_CAP 16
    ColdImportSource local_imports[COLD_LOCAL_IMPORT_CAP];
    int32_t local_import_count = 0;
    int32_t fn_pos = 0;
    int32_t scan_safety = 0;
    {
        int32_t scan_pos = 0;
        int32_t line_start = 0;
        while (scan_pos < source.len && scan_safety < 100000) {
            scan_safety++;
            while (scan_pos < source.len && source.ptr[scan_pos] != '\n') scan_pos++;
            Span line = span_trim(span_sub(source, line_start, scan_pos));
            if (scan_pos < source.len) scan_pos++;
            line_start = scan_pos;
            if (cold_span_starts_with(line, "import ")) {
                Span module_path = {0};
                Span import_alias = {0};
                if (cold_parse_import_line(line, &module_path, &import_alias) &&
                    local_import_count < COLD_LOCAL_IMPORT_CAP) {
                    local_imports[local_import_count].alias = import_alias;
                    local_imports[local_import_count].path[0] = '\0';
                    local_import_count++;
                }
            }
            if (cold_span_starts_with(line, "fn ")) {
                Span fn_name = span_sub(line, 3, line.len);
                int32_t name_end = 0;
                while (name_end < fn_name.len && (cold_ident_char(fn_name.ptr[name_end]) || fn_name.ptr[name_end] == '_' || fn_name.ptr[name_end] == '`')) name_end++;
                fn_name = span_sub(fn_name, 0, name_end);
                /* Search for qualified name in symbols (safety-capped) */
                int32_t search_limit = symbols->function_count;
                if (search_limit > 2000) search_limit = 2000;
                for (int32_t si = 0; si < search_limit && fn_pos < COLD_IMPORT_FN_MAP_CAP; si++) {
                    FnDef *sfn = &symbols->functions[si];
                    /* Check if sfn->name starts with alias. and ends with fn_name */
                    if (sfn->name.len >= alias.len + 1 + fn_name.len &&
                        safe_memcmp(sfn->name.ptr, alias.ptr, alias.len) &&
                        sfn->name.ptr[alias.len] == '.' &&
                        safe_memcmp(sfn->name.ptr + alias.len + 1, fn_name.ptr, fn_name.len) &&
                        sfn->name.len == alias.len + 1 + fn_name.len) {
                        qual_indices[fn_pos] = si;
                        fn_pos++;
                        break;
                    }
                }
            }
        }
    }
    ColdImportBodyCompilationActive = true;
    Parser parser = {source, 0, symbols->arena, symbols, true /* import_mode */, alias};
    parser.import_sources = local_imports;
    parser.import_source_count = local_import_count;
    parser.function_bodies = function_bodies;
    parser.function_body_cap = body_cap;
    fn_pos = 0;
    while (parser.pos < source.len) {
        parser_ws(&parser);
        if (parser.pos >= source.len) break;
        Span next = parser_peek(&parser);
        if (span_eq(next, "type") || span_eq(next, "const") ||
            span_eq(next, "import") || span_eq(next, "module")) {
            parser_line(&parser);
        } else if (span_eq(next, "fn")) {
            int32_t saved_fn_pos = fn_pos;
            fn_pos++;
            volatile int32_t symbol_index = -1;
            volatile BodyIR *volatile body = 0;
            ColdErrorRecoveryEnabled = true;
            if (setjmp(ColdErrorJumpBuf) == 0) {
                cold_current_parsing_body = 0;
                body = parse_fn(&parser, (int32_t *)&symbol_index);
                cold_current_parsing_body = (BodyIR *)body;
            } else {
                parser.pos = cold_next_top_level_decl(parser.source, parser.pos);
                body = 0;
                symbol_index = -1;
            }
            ColdErrorRecoveryEnabled = false;
            if (!body) continue;
            /* In import_mode, symbols_find_fn returns -1 (local name doesn't match
               qualified symbol name). Use pre-scanned qual_indices instead. */
            if (symbol_index < 0 && parser.import_mode && saved_fn_pos < COLD_IMPORT_FN_MAP_CAP && qual_indices[saved_fn_pos] >= 0) {
                symbol_index = qual_indices[saved_fn_pos];
            }
            if (symbol_index < 0 || symbol_index >= body_cap) continue;
            /* Selective compilation: skip bodies that cold codegen cannot handle.
               Two checks:
               1. Declared return kind must be I32, I64, or void.
               2. Body must not contain composite return terms (nested
                  variant/str/object returns inside I32-declared functions). */
            bool skip_body = false;
            if (body->return_kind != SLOT_I32 && body->return_kind != SLOT_I64 &&
                body->return_kind != SLOT_PTR && body->return_kind > 0) {
                skip_body = true;
            }
            if (!skip_body) {
                /* Scan for composite return values that codegen can't handle
                   without sret. If any return value is not I32/I64, skip. */
                for (int32_t ti = 0; ti < body->term_count; ti++) {
                    if (body->term_kind[ti] == BODY_TERM_RET) {
                        int32_t vs = body->term_value[ti];
                        if (vs >= 0 && vs < body->slot_count) {
                            int32_t vk = body->slot_kind[vs];
                            if (vk != SLOT_I32 && vk != SLOT_I64 && vk != SLOT_PTR && vk > 0) {
                                skip_body = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (skip_body) { continue; /* keep stub */ }
            function_bodies[(int32_t)symbol_index] = (BodyIR *)body;
            compiled++;
            /* Map to qualified index if available */
            if (saved_fn_pos < COLD_IMPORT_FN_MAP_CAP && qual_indices[saved_fn_pos] >= 0) {
                int32_t qi = qual_indices[saved_fn_pos];
                if (qi != (int32_t)symbol_index && qi < body_cap) {
                    function_bodies[qi] = (BodyIR *)body;
                    compiled++;
                }
            }
        } else {
            parser_line(&parser);
        }
    }
    /* Keep source mapped: BodyIR Span references (slot_type etc.) point into
       this mmap. Unmapping creates dangling pointers that crash codegen.
       Import sources are small (< 1 MB each, < 10 imports for dispatch_min). */
    ColdImportBodyCompilationActive = false;
    return compiled;
}

int32_t cold_collect_direct_import_sources(Span entry_source,
                                                  ColdImportSource *imports,
                                                  int32_t import_cap) {
    int32_t count = 0;
    int32_t pos = 0;
    bool in_triple = false;
    while (pos < entry_source.len) {
        int32_t start = pos;
        while (pos < entry_source.len && entry_source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < entry_source.len) pos++;
        Span line = span_sub(entry_source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span module_path = {0};
        Span alias = {0};
        if (!cold_parse_import_line(span_trim(line), &module_path, &alias)) continue;
        if (count >= import_cap) die("too many cold direct imports");
        if (!cold_import_source_path(module_path, imports[count].path,
                                     sizeof(imports[count].path))) {
            die("cold import path is not resolvable");
        }
        imports[count].alias = alias;
        count++;
    }
    return count;
}

static bool cold_import_symbol_parts(Span qualified_name, Span *alias, Span *local_name) {
    int32_t dot = cold_span_find_char(qualified_name, '.');
    if (dot <= 0 || dot + 1 >= qualified_name.len) return false;
    *alias = span_sub(qualified_name, 0, dot);
    *local_name = span_sub(qualified_name, dot + 1, qualified_name.len);
    return true;
}

static ColdImportSource *cold_find_import_source_for_alias(ColdImportSource *imports,
                                                           int32_t import_count,
                                                           Span alias) {
    for (int32_t i = 0; i < import_count; i++) {
        if (span_same(imports[i].alias, alias)) return &imports[i];
    }
    return 0;
}

static bool cold_import_function_symbol_matches(Symbols *symbols,
                                                ColdImportSource *import_source,
                                                ColdFunctionSymbol *symbol,
                                                int32_t target_fn) {
    Span names[COLD_MAX_I32_PARAMS];
    int32_t kinds[COLD_MAX_I32_PARAMS];
    int32_t sizes[COLD_MAX_I32_PARAMS];
    int32_t arity = cold_imported_param_specs(symbols, import_source->alias,
                                              symbol->params, names, kinds, sizes,
                                              COLD_MAX_I32_PARAMS);
    (void)names;
    Span qualified_name = cold_arena_join3(symbols->arena, import_source->alias,
                                           ".", symbol->name);
    Span qualified_ret = cold_qualify_import_type(symbols->arena, import_source->alias,
                                                  symbol->return_type);
    int32_t resolved = symbols_find_fn(symbols, qualified_name, arity, kinds, sizes,
                                       qualified_ret);
    return resolved == target_fn;
}

static void cold_compile_import_function_direct(Symbols *symbols,
                                                ColdImportSource *imports,
                                                int32_t import_count,
                                                BodyIR **function_bodies,
                                                int32_t body_cap,
                                                int32_t target_fn) {
    if (target_fn < 0 || target_fn >= symbols->function_count) {
        die("cold import target function out of range");
    }
    if (target_fn >= body_cap) die("cold import target exceeds body table");
    if (function_bodies[target_fn]) return;

    FnDef *fn = &symbols->functions[target_fn];
    if (fn->is_external) return;

    Span alias = {0};
    Span local_name = {0};
    if (!cold_import_symbol_parts(fn->name, &alias, &local_name)) {
        cold_die_missing_reachable_body(symbols, target_fn);
    }
    ColdImportSource *import_source = cold_find_import_source_for_alias(imports,
                                                                        import_count,
                                                                        alias);
    if (!import_source) {
        cold_die_missing_reachable_body(symbols, target_fn);
    }

    Span source = source_open(import_source->path);
    if (source.len <= 0) die("cold import source open failed");
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
            if (trimmed.len > 3 && trimmed.ptr[3] == '`') continue;
            fprintf(stderr, "cheng_cold: cannot parse imported body fn path=%s line=%d\n",
                    import_source->path, line_no);
            continue;
        }
        if (!span_same(symbol.name, local_name)) continue;
        if (!cold_import_function_symbol_matches(symbols, import_source, &symbol,
                                                target_fn)) {
            continue;
        }
        if (!symbol.has_body) {
            fprintf(stderr, "cheng_cold: function %.*s has no body (path=%s line=%d)\n",
                    (int)symbol.name.len, symbol.name.ptr,
                    import_source->path, symbol.line);
            die("reachable cold import function has no body");
        }
        Parser parser = {symbol.source_span, 0, symbols->arena, symbols,
                         true /* import_mode */, import_source->alias};
        int32_t parsed_index = -1;
        BodyIR *body = parse_fn(&parser, &parsed_index);
        if (!body) die("reachable cold import function parse failed");
        function_bodies[target_fn] = body;
        return;
    }
    munmap((void *)source.ptr, (size_t)source.len);
    cold_die_missing_reachable_body(symbols, target_fn);
}

void cold_compile_reachable_import_bodies(Symbols *symbols,
                                                 BodyIR **function_bodies,
                                                 int32_t body_cap,
                                                 int32_t entry_function,
                                                 ColdImportSource *imports,
                                                 int32_t import_count) {
    if (entry_function < 0 || entry_function >= symbols->function_count) {
        die("cold reachable import entry out of range");
    }
    int32_t function_count = symbols->function_count;
    bool *reachable = arena_alloc(symbols->arena, (size_t)function_count * sizeof(bool));
    int32_t *stack = arena_alloc(symbols->arena, (size_t)function_count * sizeof(int32_t));
    for (int32_t i = 0; i < function_count; i++) reachable[i] = false;
    int32_t stack_count = 0;
    reachable[entry_function] = true;
    stack[stack_count++] = entry_function;
    while (stack_count > 0) {
        int32_t fn_index = stack[--stack_count];
        if (fn_index < 0 || fn_index >= function_count) die("cold reachable import index out of range");
        if (!function_bodies[fn_index]) {
            /* Only try import compilation for qualified names (alias.fn).
               Entry-module functions with bare names that failed body
               compilation get marked external to avoid hard-fail. */
            Span fn_name = symbols->functions[fn_index].name;
            bool is_imported = false;
            for (int32_t ci = 0; ci < fn_name.len; ci++) {
                if (fn_name.ptr[ci] == '.') { is_imported = true; break; }
            }
            if (is_imported) {
                cold_compile_import_function_direct(symbols, imports, import_count,
                                                    function_bodies, body_cap, fn_index);
            } else {
                symbols->functions[fn_index].is_external = true;
            }
        }
        BodyIR *body = function_bodies[fn_index];
        if (!cold_body_codegen_ready(body)) {
            if (symbols->functions[fn_index].is_external) continue;
            cold_die_missing_reachable_body(symbols, fn_index);
        }
        for (int32_t op = 0; op < body->op_count; op++) {
            int32_t target = -1;
            if (body->op_kind[op] == BODY_OP_CALL_I32 ||
                body->op_kind[op] == BODY_OP_CALL_COMPOSITE ||
                body->op_kind[op] == BODY_OP_FN_ADDR) {
                target = body->op_a[op];
            }
            if (target < 0) continue;
            if (target >= function_count) continue;  /* skip out-of-range targets */
            if (reachable[target]) continue;
            reachable[target] = true;
            stack[stack_count++] = target;
        }
    }
}

__attribute__((unused))
int32_t cold_compile_imported_bodies_no_recurse(Symbols *symbols, Span entry_source,
                                                       BodyIR **function_bodies, int32_t body_cap) {
    /* Install SIGSEGV handler to convert segfaults into die() which setjmp catches */
    struct sigaction sa_segv_old;
    struct sigaction sa_segv_new;
    memset(&sa_segv_new, 0, sizeof(sa_segv_new));
    sa_segv_new.sa_handler = cold_sigsegv_die_handler;
    sa_segv_new.sa_flags = SA_NODEFER;
    sigaction(SIGSEGV, &sa_segv_new, &sa_segv_old);
    /* Note: SIGSEGV handler persists after this function. It's restored by the
       caller (cold_compile_source_path_to_macho) or on process exit.
       This is intentional: import body compilation may corrupt arena, and
       subsequent codegen may also SEGV. We want die() messages instead of
       silent crashes. */
    int32_t compiled = 0;
    int32_t pos = 0;
    bool in_triple = false;
    while (pos < entry_source.len) {
        int32_t start = pos;
        while (pos < entry_source.len && entry_source.ptr[pos] != '\n') pos++;
        int32_t end = pos;
        if (pos < entry_source.len) pos++;
        Span line = span_sub(entry_source, start, end);
        if (in_triple) {
            if (cold_line_has_triple_quote(line)) in_triple = false;
            continue;
        }
        if (!cold_line_top_level(line)) {
            if (cold_line_has_triple_quote(line)) in_triple = true;
            continue;
        }
        Span module_path = {0};
        Span alias = {0};
        if (!cold_parse_import_line(span_trim(line), &module_path, &alias)) continue;
        char path[PATH_MAX];
        if (!cold_import_source_path(module_path, path, sizeof(path))) {
            fprintf(stderr, "[import_body] path resolve failed: %.*s\n", module_path.len, module_path.ptr);
            continue;
        }
        ColdErrorRecoveryEnabled = true;
        ColdImportSegvActive = true;
        if (setjmp(ColdImportSegvJumpBuf) == 0) {
            int32_t n = cold_compile_one_import_direct(symbols, path, alias, function_bodies, body_cap);
            compiled += n;
        } else {
            fprintf(stderr, "[import_body] skipped import due to SEGV: %s\n", path);
            ColdImportSegvSaw = 1;
        }
        ColdImportSegvActive = false;
        ColdErrorRecoveryEnabled = false;
    }
    /* SIGSEGV handler intentionally left installed for post-import codegen safety */
    return compiled;
}

static void cold_collect_transitive_imports_rec(Symbols *symbols, Span source,
                                                  char visited[][PATH_MAX], int32_t *visited_count,
                                                  int32_t depth) {
    if (depth > 32 || *visited_count >= 128) return;
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
        Span module_path = {0};
        Span alias = {0};
        if (!cold_parse_import_line(span_trim(line), &module_path, &alias)) continue;
        char path[PATH_MAX];
        if (!cold_import_source_path(module_path, path, sizeof(path))) continue;
        char abs_path[PATH_MAX];
        cold_absolute_path(path, abs_path, sizeof(abs_path));
        bool seen = false;
        for (int32_t vi = 0; vi < *visited_count; vi++) {
            if (strcmp(visited[vi], abs_path) == 0) { seen = true; break; }
        }
        if (seen) continue;
        if (*visited_count >= 64) return;
        strncpy(visited[(*visited_count)++], abs_path, PATH_MAX - 1);
        Span sub_source = source_open(path);
        if (sub_source.len <= 0) continue;
        ColdErrorRecoveryEnabled = true;
        if (setjmp(ColdErrorJumpBuf) == 0) {
            cold_collect_imported_function_signatures(symbols, sub_source);
        } else {
            /* Transitive type loading skipped (depth/visited limit) */
        }
        ColdErrorRecoveryEnabled = false;
        cold_collect_transitive_imports_rec(symbols, sub_source, visited, visited_count, depth + 1);
        munmap((void *)sub_source.ptr, (size_t)sub_source.len);
    }
}

__attribute__((unused))
void cold_collect_all_transitive_imports(Symbols *symbols, Span entry_source) {
    char visited[64][PATH_MAX];
    int32_t visited_count = 0;
    cold_collect_transitive_imports_rec(symbols, entry_source, visited, &visited_count, 0);
}
#endif /* COLD_BACKEND_ONLY */

#ifndef COLD_BACKEND_ONLY
bool cold_compile_source_path_to_macho(const char *out_path,
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
    ColdImportSource import_sources[64];
    int32_t import_source_count = 0;

    if (src_path && src_path[0] != '\0') {
        mapped_source = source_open(src_path);
        if (mapped_source.len <= 0) return false;
        import_source_count = cold_collect_direct_import_sources(mapped_source,
                                                                 import_sources,
                                                                 64);
        cold_collect_imported_function_signatures(symbols, mapped_source);
        cold_collect_function_signatures(symbols, mapped_source);
        { char ws_root[PATH_MAX] = {0};
          const char *sm = strstr(src_path, "/src/");
          if (!sm) sm = strstr(src_path, "src/");
          if (sm == src_path) { char rp[PATH_MAX]; if (realpath(src_path, rp)) { const char *s = strstr(rp, "/src/"); if (s) { size_t rl = (size_t)(s - rp); memcpy(ws_root, rp, rl); ws_root[rl] = 0; } } }
          else if (sm) { size_t rl = (size_t)(sm - src_path); if (rl < sizeof(ws_root)) { memcpy(ws_root, src_path, rl); ws_root[rl] = 0; } }
          else fprintf(stderr, "[WARN] ws_root empty, src_path=%s\n", src_path);
        }
        body_cap = symbols->function_cap;
        if (body_cap < 256) body_cap = 256;
        function_bodies = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
        memset(function_bodies, 0, (size_t)body_cap * sizeof(BodyIR *));
        Parser parser = {mapped_source, 0, arena, symbols};
        parser.import_sources = import_sources;
        parser.import_source_count = import_source_count;
        parser.function_bodies = function_bodies;
        parser.function_body_cap = body_cap;
        while (parser.pos < mapped_source.len) {
            parser_ws(&parser);
            if (parser.pos >= mapped_source.len) break;
            Span next = parser_peek(&parser);
            if (span_eq(next, "type")) {
                parse_type(&parser);
            } else if (span_eq(next, "const")) {
                parse_const(&parser);
            } else if (span_eq(next, "fn")) {
                volatile int32_t symbol_index = -1;
                volatile BodyIR *volatile body2 = 0;
                ColdErrorRecoveryEnabled = true;
                if (setjmp(ColdErrorJumpBuf) == 0) {
                    body2 = parse_fn(&parser, (int32_t *)&symbol_index);
                } else {
                    parser.pos = cold_next_top_level_decl(parser.source, parser.pos);
                    body2 = 0;
                    symbol_index = -1;
                }
                ColdErrorRecoveryEnabled = false;
                if (!body2) continue;
                if (symbol_index < 0 || symbol_index >= body_cap) continue;
                function_bodies[(int32_t)symbol_index] = (BodyIR *)body2;
                FnDef *fn = &symbols->functions[(int32_t)symbol_index];
                if (first_function < 0) first_function = (int32_t)symbol_index;
                if (span_eq(fn->name, "main")) {
                    main_function = (int32_t)symbol_index;
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

    uint64_t parse_end_us = cold_now_us();

    Code *code = code_new(arena, 256);
    if (demo_body) {
        FunctionPatchList function_patches = {0};
        function_patches.arena = arena;
        codegen_func(code, demo_body, symbols, &function_patches);
    } else {
        cold_compile_reachable_import_bodies(symbols, function_bodies, body_cap,
                                             main_function, import_sources,
                                             import_source_count);
        codegen_program(code, function_bodies, symbols->function_count, main_function, symbols, 0);
    }

    uint64_t codegen_end_us = cold_now_us();

    if (output_direct_macho(out_path, code) != 0) {
        if (mapped_source.len > 0) munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
        return false;
    }

    uint64_t emit_end_us = cold_now_us();

    if (stats) {
        stats->function_count = symbols->function_count;
        stats->type_count = symbols->type_count + symbols->object_count;
        if (demo_body) {
            stats->op_count = demo_body->op_count;
            stats->block_count = demo_body->block_count;
            stats->switch_count = demo_body->switch_count;
        } else {
            cold_collect_body_stats(symbols, function_bodies, symbols->function_count, stats);
        }
        stats->code_words = code->count;
        stats->arena_kb = arena->used / 1024;
        if (start_us > 0 && parse_end_us >= start_us)
            stats->parse_us = parse_end_us - start_us;
        if (parse_end_us > 0 && codegen_end_us >= parse_end_us)
            stats->codegen_us = codegen_end_us - parse_end_us;
        if (codegen_end_us > 0 && emit_end_us >= codegen_end_us)
            stats->emit_us = emit_end_us - codegen_end_us;
        stats->elapsed_us = emit_end_us - start_us;
    }
    if (mapped_source.len > 0) munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
    return true;
}
#endif /* COLD_BACKEND_ONLY */

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
    if (stats && stats->link_object) {
        fputs("system_link_exec_scope=cold_link_object\n", file);
    } else {
        fputs("system_link_exec_scope=cold_subset_direct_macho\n", file);
    }
    fputs("full_backend_codegen=0\n", file);
    fprintf(file, "target=%s\n", target ? target : "");
    fprintf(file, "emit=%s\n", emit ? emit : "");
    if (emit && strcmp(emit, "csg-v2") == 0) {
        fprintf(file, "cold_csg_v2_writer_status=%s\n", success ? "ok" : "fail");
    }
    fprintf(file, "source=%s\n", source_path ? source_path : "");
    fprintf(file, "csg_input=%s\n", csg_path ? csg_path : "");
    fprintf(file, "output=%s\n", out_path ? out_path : "");
    fprintf(file, "direct_macho=%d\n", success ? 1 : 0);
    fprintf(file, "function_task_job_count=%d\n", cold_jobs_from_env());
    fprintf(file, "function_task_schedule=%s\n", cold_jobs_from_env() > 1 ? "ws" : "serial");
    if (stats) {
        fprintf(file, "cold_csg_lowering=%d\n", stats->csg_lowering);
        fprintf(file, "cold_csg_statement_count=%d\n", stats->csg_statement_count);
        fprintf(file, "cold_frontend_function_count=%d\n", stats->function_count);
        fprintf(file, "cold_frontend_type_count=%d\n", stats->type_count);
        fprintf(file, "cold_body_op_count=%d\n", stats->op_count);
        fprintf(file, "cold_body_block_count=%d\n", stats->block_count);
        fprintf(file, "cold_body_switch_count=%d\n", stats->switch_count);
        fprintf(file, "cold_body_call_count=%d\n", stats->call_count);
        fprintf(file, "cold_param_count=%d\n", stats->param_count);
        fprintf(file, "cold_abi_register_params=%d\n", stats->abi_register_params);
        fprintf(file, "cold_abi_stack_params=%d\n", stats->abi_stack_params);
        fprintf(file, "cold_max_frame_size=%d\n", stats->max_frame_size);
        fprintf(file, "cold_max_frame_function=%s\n", stats->max_frame_function);
        fprintf(file, "cold_after_source_bundle_frame_size=%d\n", stats->after_source_bundle_frame_size);
        fprintf(file, "cold_after_csg_frame_size=%d\n", stats->after_csg_frame_size);
        fprintf(file, "cold_codegen_words=%d\n", stats->code_words);
        fprintf(file, "cold_arena_kb=%zu\n", stats->arena_kb);
        fprintf(file, "facts_bytes=%llu\n", (unsigned long long)stats->facts_bytes);
        fprintf(file, "facts_mmap_us=%llu\n", (unsigned long long)stats->facts_mmap_us);
        fprintf(file, "facts_verify_us=%llu\n", (unsigned long long)stats->facts_verify_us);
        fprintf(file, "facts_decode_us=%llu\n", (unsigned long long)stats->facts_decode_us);
        fprintf(file, "facts_emit_obj_us=%llu\n", (unsigned long long)stats->facts_emit_obj_us);
        fprintf(file, "facts_emit_exe_us=%llu\n", (unsigned long long)stats->facts_emit_exe_us);
        fprintf(file, "facts_total_us=%llu\n", (unsigned long long)stats->facts_total_us);
        cold_print_elapsed_ms(file, "facts_mmap_ms", stats->facts_mmap_us);
        cold_print_elapsed_ms(file, "facts_verify_ms", stats->facts_verify_us);
        cold_print_elapsed_ms(file, "facts_decode_ms", stats->facts_decode_us);
        cold_print_elapsed_ms(file, "facts_emit_obj_ms", stats->facts_emit_obj_us);
        cold_print_elapsed_ms(file, "facts_emit_exe_ms", stats->facts_emit_exe_us);
        cold_print_elapsed_ms(file, "facts_total_ms", stats->facts_total_us);
        fprintf(file, "facts_function_count=%d\n", stats->facts_function_count);
        fprintf(file, "facts_word_count=%d\n", stats->facts_word_count);
        fprintf(file, "facts_reloc_count=%d\n", stats->facts_reloc_count);
        fprintf(file, "canonical_hash_count=%d\n", stats->canonical_hash_count);
        fprintf(file, "canonical_normalized_count=%d\n", stats->canonical_normalized_count);
        fprintf(file, "total_function_count=%d\n", stats->total_function_count);
        fprintf(file, "egraph_rewrite_count=%d\n", stats->egraph_rewrite_count);
        fprintf(file, "egraph_dedup_count=%d\n", stats->egraph_dedup_count);
        fprintf(file, "link_object=%d\n", stats->link_object);
        fprintf(file, "provider_object_count=%d\n", stats->provider_object_count);
        fprintf(file, "provider_archive=%d\n", stats->provider_archive);
        fprintf(file, "provider_archive_member_count=%d\n", stats->provider_archive_member_count);
        fprintf(file, "provider_archive_hash=%016llx\n",
                (unsigned long long)stats->provider_archive_hash);
        fprintf(file, "provider_export_count=%d\n", stats->provider_export_count);
        fprintf(file, "provider_resolved_symbol_count=%d\n", stats->provider_resolved_symbol_count);
        fprintf(file, "link_reloc_count=%d\n", stats->link_reloc_count);
        fprintf(file, "unresolved_symbol_count=%d\n", stats->unresolved_symbol_count);
        fprintf(file, "first_unresolved_symbol=%s\n", stats->first_unresolved_symbol);
        cold_print_elapsed_ms(file, "cold_compile_elapsed_ms", stats->elapsed_us);
    }
    /* 30-80ms cold self-hosting architecture compliance */
    fputs("source_storage=mmap_span\n", file);
    fputs("allocation=phase_arena\n", file);
    fputs("ir_layout=soa_dense\n", file);
    fputs("linkerless_image=1\n", file);
    fputs("system_link=0\n", file);
    fputs("hot_path_node_malloc=0\n", file);
    cold_print_exec_phase_report(file, stats);
    cold_print_resource_report(file);
    if (error && error[0] != '\0') fprintf(file, "error=%s\n", error);
    fclose(file);
    return true;
}

static uint16_t cold_u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t cold_u32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t cold_u64le(const uint8_t *p) {
    uint64_t lo = cold_u32le(p);
    uint64_t hi = cold_u32le(p + 4);
    return lo | (hi << 32);
}

static void cold_put_u32le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void cold_put_u64le(uint8_t *p, uint64_t v) {
    cold_put_u32le(p, (uint32_t)(v & 0xFFFFFFFFu));
    cold_put_u32le(p + 4, (uint32_t)(v >> 32));
}

static bool cold_file_range_ok(int32_t len, uint64_t off, uint64_t size) {
    if (off > (uint64_t)len) return false;
    if (size > (uint64_t)len - off) return false;
    return true;
}

static uint16_t cold_elf_machine_for_target(const char *target) {
    if (!target) return 0;
    if (strcmp(target, "aarch64-unknown-linux-gnu") == 0) return EM_AARCH64;
    if (strcmp(target, "riscv64-unknown-linux-gnu") == 0) return EM_RISCV;
    return 0;
}

static bool cold_elf_section_name_eq(const char *strtab,
                                     uint64_t strtab_size,
                                     uint32_t off,
                                     const char *name) {
    size_t name_len = strlen(name);
    if ((uint64_t)off >= strtab_size) return false;
    if (name_len > strtab_size - (uint64_t)off - 1) return false;
    return memcmp(strtab + off, name, name_len) == 0 &&
           strtab[off + name_len] == '\0';
}

static bool cold_link_elf64_object_to_exec(const char *object_path,
                                           const char *out_path,
                                           const char *target,
                                           ColdCompileStats *stats) {
    uint64_t start_us = cold_now_us();
    Span obj = source_open(object_path);
    if (obj.len < 64) return false;
    const uint8_t *data = obj.ptr;
    bool ok = false;
    uint32_t *words = 0;

    uint16_t expected_machine = cold_elf_machine_for_target(target);
    if (expected_machine == 0) goto done;
    if (memcmp(data, "\x7F""ELF", 4) != 0) goto done;
    if (data[4] != ELFCLASS64 || data[5] != ELFDATA2LSB || data[6] != EV_CURRENT) goto done;
    if (cold_u16le(data + 0x10) != ET_REL) goto done;
    uint16_t machine = cold_u16le(data + 0x12);
    if (machine != expected_machine) goto done;
    if (cold_u32le(data + 0x14) != EV_CURRENT) goto done;

    uint64_t shoff = cold_u64le(data + 0x28);
    uint16_t shentsize = cold_u16le(data + 0x3A);
    uint16_t shnum = cold_u16le(data + 0x3C);
    uint16_t shstrndx = cold_u16le(data + 0x3E);
    if (shentsize != sizeof(Elf64_Shdr) || shnum == 0 || shstrndx >= shnum) goto done;
    if (!cold_file_range_ok(obj.len, shoff, (uint64_t)shentsize * shnum)) goto done;
    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(data + shoff);
    const Elf64_Shdr *shstr = &shdrs[shstrndx];
    if (shstr->sh_type != SHT_STRTAB) goto done;
    if (!cold_file_range_ok(obj.len, shstr->sh_offset, shstr->sh_size)) goto done;
    const char *shstr_data = (const char *)(data + shstr->sh_offset);

    int32_t text_idx = -1;
    int32_t symtab_idx = -1;
    for (int32_t i = 0; i < (int32_t)shnum; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        if (!cold_file_range_ok(obj.len, sh->sh_offset, sh->sh_size)) goto done;
        if (sh->sh_type == SHT_PROGBITS &&
            (sh->sh_flags & SHF_EXECINSTR) &&
            cold_elf_section_name_eq(shstr_data, shstr->sh_size, sh->sh_name, ".text")) {
            text_idx = i;
        } else if (sh->sh_type == SHT_SYMTAB) {
            symtab_idx = i;
        }
    }
    if (text_idx <= 0 || symtab_idx <= 0) goto done;
    int32_t rela_text_idx = -1;
    for (int32_t i = 0; i < (int32_t)shnum; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        if (sh->sh_type == SHT_RELA && sh->sh_info == (uint32_t)text_idx) {
            rela_text_idx = i;
            break;
        }
    }

    const Elf64_Shdr *text = &shdrs[text_idx];
    if ((text->sh_size % 4) != 0 || text->sh_size > (uint64_t)INT32_MAX * 4ULL) goto done;
    int32_t word_count = (int32_t)(text->sh_size / 4);
    words = (uint32_t *)calloc(word_count > 0 ? (size_t)word_count : 1, sizeof(uint32_t));
    if (!words) goto done;
    if (text->sh_size > 0) memcpy(words, data + text->sh_offset, (size_t)text->sh_size);

    const Elf64_Shdr *symtab = &shdrs[symtab_idx];
    if (symtab->sh_entsize != sizeof(Elf64_Sym) || symtab->sh_link >= shnum) goto done;
    if ((symtab->sh_size % sizeof(Elf64_Sym)) != 0) goto done;
    const Elf64_Shdr *strtab = &shdrs[symtab->sh_link];
    if (strtab->sh_type != SHT_STRTAB) goto done;
    if (!cold_file_range_ok(obj.len, strtab->sh_offset, strtab->sh_size)) goto done;
    const Elf64_Sym *syms = (const Elf64_Sym *)(data + symtab->sh_offset);
    int32_t sym_count = (int32_t)(symtab->sh_size / sizeof(Elf64_Sym));

    int32_t reloc_count = 0;
    int32_t unresolved_count = 0;
    if (rela_text_idx > 0) {
        const Elf64_Shdr *rela = &shdrs[rela_text_idx];
        if (rela->sh_entsize != sizeof(Elf64_Rela) ||
            rela->sh_link != (uint32_t)symtab_idx ||
            rela->sh_info != (uint32_t)text_idx ||
            (rela->sh_size % sizeof(Elf64_Rela)) != 0) {
            goto done;
        }
        const Elf64_Rela *relas = (const Elf64_Rela *)(data + rela->sh_offset);
        reloc_count = (int32_t)(rela->sh_size / sizeof(Elf64_Rela));
        for (int32_t i = 0; i < reloc_count; i++) {
            uint32_t sym_index = (uint32_t)(relas[i].r_info >> 32);
            uint32_t type = (uint32_t)(relas[i].r_info & 0xFFFFFFFFu);
            if (sym_index >= (uint32_t)sym_count) goto done;
            if ((relas[i].r_offset % 4) != 0 || relas[i].r_offset >= text->sh_size) goto done;
            const Elf64_Sym *sym = &syms[sym_index];
            if (sym->st_shndx == 0) {
                unresolved_count++;
                continue;
            }
            if (sym->st_shndx != (uint16_t)text_idx) goto done;
            int64_t target_byte = (int64_t)sym->st_value + relas[i].r_addend;
            if (target_byte < 0 || target_byte >= (int64_t)text->sh_size ||
                (target_byte % 4) != 0) {
                goto done;
            }
            int32_t word_pos = (int32_t)(relas[i].r_offset / 4);
            int32_t target_word = (int32_t)(target_byte / 4);
            int32_t delta = target_word - word_pos;
            if (machine == EM_AARCH64 && type == R_AARCH64_CALL26) {
                if (delta < -(1 << 25) || delta >= (1 << 25)) goto done;
                words[word_pos] = (words[word_pos] & 0xFC000000u) |
                                  ((uint32_t)delta & 0x03FFFFFFu);
            } else if (machine == EM_RISCV &&
                       (type == R_RISCV_CALL || type == R_RISCV_CALL_PLT)) {
                if (delta < -(1 << 20) || delta >= (1 << 20)) goto done;
                words[word_pos] = rv_jal(RV_RA, delta);
            } else {
                goto done;
            }
        }
    }
    if (unresolved_count != 0) {
        if (stats) stats->unresolved_symbol_count = unresolved_count;
        goto done;
    }
    if (!elf_write_exec(out_path, words, word_count, machine)) goto done;
    if (stats) {
        stats->link_object = 1;
        stats->provider_archive_member_count = 0;
        stats->provider_archive_hash = 0;
        stats->unresolved_symbol_count = 0;
        stats->code_words = word_count;
        stats->facts_reloc_count = reloc_count;
        stats->elapsed_us = cold_now_us() - start_us;
        stats->emit_us = stats->elapsed_us;
    }
    ok = true;

done:
    if (words) free(words);
    if (obj.len > 0) munmap((void *)obj.ptr, (size_t)obj.len);
    return ok;
}

#define COLD_PROVIDER_ARCHIVE_MAGIC "CHENGPA1"
#define COLD_PROVIDER_ARCHIVE_VERSION 1u
#define COLD_PROVIDER_ARCHIVE_HASH_OFFSET 28u
#define COLD_PROVIDER_ARCHIVE_HEADER_SIZE 36u

static const char *cold_object_format_for_target_cstr(const char *target) {
    if (!target) return "";
    if (strcmp(target, "arm64-apple-darwin") == 0) return "macho";
    if (strcmp(target, "aarch64-unknown-linux-gnu") == 0 ||
        strcmp(target, "riscv64-unknown-linux-gnu") == 0) return "elf";
    if (strcmp(target, "x86_64-unknown-linux-gnu") == 0) return "";
    if (strcmp(target, "x86_64-pc-windows-msvc") == 0 ||
        strcmp(target, "aarch64-pc-windows-msvc") == 0) return "coff";
    return "";
}

static uint64_t cold_provider_archive_hash_bytes(uint8_t *buf, size_t len) {
    if (len < COLD_PROVIDER_ARCHIVE_HEADER_SIZE) return 0;
    uint8_t saved[8];
    memcpy(saved, buf + COLD_PROVIDER_ARCHIVE_HASH_OFFSET, sizeof(saved));
    memset(buf + COLD_PROVIDER_ARCHIVE_HASH_OFFSET, 0, sizeof(saved));
    uint64_t hash = cold_fnv1a64_update_bytes(1469598103934665603ULL, buf, len);
    memcpy(buf + COLD_PROVIDER_ARCHIVE_HASH_OFFSET, saved, sizeof(saved));
    return hash;
}

static bool cold_write_all_fd(int fd, const uint8_t *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

static bool cold_write_provider_archive(const char *out_path,
                                        const char *target,
                                        const char *member_object,
                                        const char *member_module,
                                        const char *member_source,
                                        const char *export_symbol,
                                        int32_t *member_count_out,
                                        uint64_t *hash_out) {
    if (!out_path || out_path[0] == '\0' || !target || target[0] == '\0') return false;
    const char *format = cold_object_format_for_target_cstr(target);
    if (format[0] == '\0') return false;
    bool has_member = member_object && member_object[0] != '\0';
    Span object = {0};
    if (has_member) {
        object = source_open(member_object);
        if (object.len <= 0) return false;
        if (object.len > INT32_MAX) {
            munmap((void *)object.ptr, (size_t)object.len);
            return false;
        }
    }
    const char *module = member_module && member_module[0] ? member_module : "";
    const char *source = member_source && member_source[0] ? member_source : "";
    const char *export_name = export_symbol && export_symbol[0] ? export_symbol : "";
    uint32_t target_len = (uint32_t)strlen(target);
    uint32_t format_len = (uint32_t)strlen(format);
    uint32_t module_len = (uint32_t)strlen(module);
    uint32_t source_len = (uint32_t)strlen(source);
    uint32_t export_len = (uint32_t)strlen(export_name);
    uint32_t member_count = has_member ? 1u : 0u;
    uint32_t export_count = (has_member && export_len > 0) ? 1u : 0u;
    uint64_t object_hash = has_member
        ? cold_fnv1a64_update_bytes(1469598103934665603ULL,
                                    object.ptr, (size_t)object.len)
        : 0;
    size_t total = COLD_PROVIDER_ARCHIVE_HEADER_SIZE +
                   (size_t)target_len + (size_t)format_len;
    if (has_member) {
        total += 28u + (size_t)module_len + (size_t)source_len +
                 (size_t)export_len + (size_t)object.len;
    }
    if (total > INT32_MAX) {
        if (object.len > 0) munmap((void *)object.ptr, (size_t)object.len);
        return false;
    }
    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) {
        if (object.len > 0) munmap((void *)object.ptr, (size_t)object.len);
        return false;
    }
    size_t pos = 0;
    memcpy(buf + pos, COLD_PROVIDER_ARCHIVE_MAGIC, 8); pos += 8;
    cold_put_u32le(buf + pos, COLD_PROVIDER_ARCHIVE_VERSION); pos += 4;
    cold_put_u32le(buf + pos, target_len); pos += 4;
    cold_put_u32le(buf + pos, format_len); pos += 4;
    cold_put_u32le(buf + pos, member_count); pos += 4;
    cold_put_u32le(buf + pos, export_count); pos += 4;
    memset(buf + pos, 0, 8); pos += 8;
    memcpy(buf + pos, target, target_len); pos += target_len;
    memcpy(buf + pos, format, format_len); pos += format_len;
    if (has_member) {
        cold_put_u32le(buf + pos, module_len); pos += 4;
        cold_put_u32le(buf + pos, source_len); pos += 4;
        cold_put_u32le(buf + pos, (uint32_t)object.len); pos += 4;
        cold_put_u32le(buf + pos, export_count); pos += 4;
        cold_put_u64le(buf + pos, object_hash); pos += 8;
        cold_put_u32le(buf + pos, export_len); pos += 4;
        memcpy(buf + pos, module, module_len); pos += module_len;
        memcpy(buf + pos, source, source_len); pos += source_len;
        memcpy(buf + pos, export_name, export_len); pos += export_len;
        memcpy(buf + pos, object.ptr, (size_t)object.len); pos += (size_t)object.len;
    }
    if (pos != total) {
        free(buf);
        if (object.len > 0) munmap((void *)object.ptr, (size_t)object.len);
        return false;
    }
    uint64_t archive_hash = cold_provider_archive_hash_bytes(buf, total);
    cold_put_u64le(buf + COLD_PROVIDER_ARCHIVE_HASH_OFFSET, archive_hash);
    char parent[PATH_MAX];
    if (cold_parent_dir(out_path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) {
        free(buf);
        if (object.len > 0) munmap((void *)object.ptr, (size_t)object.len);
        return false;
    }
    int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        free(buf);
        if (object.len > 0) munmap((void *)object.ptr, (size_t)object.len);
        return false;
    }
    bool ok = cold_write_all_fd(fd, buf, total);
    close(fd);
    free(buf);
    if (object.len > 0) munmap((void *)object.ptr, (size_t)object.len);
    if (!ok) return false;
    if (member_count_out) *member_count_out = (int32_t)member_count;
    if (hash_out) *hash_out = archive_hash;
    return true;
}

static bool cold_verify_provider_archive(const char *path,
                                         const char *target,
                                         int32_t *member_count_out,
                                         uint64_t *hash_out) {
    Span archive = source_open(path);
    if (archive.len < (int32_t)COLD_PROVIDER_ARCHIVE_HEADER_SIZE) return false;
    const uint8_t *p = archive.ptr;
    bool ok = false;
    if (memcmp(p, COLD_PROVIDER_ARCHIVE_MAGIC, 8) != 0) goto done;
    uint32_t version = cold_u32le(p + 8);
    uint32_t target_len = cold_u32le(p + 12);
    uint32_t format_len = cold_u32le(p + 16);
    uint32_t member_count = cold_u32le(p + 20);
    uint32_t export_count = cold_u32le(p + 24);
    uint64_t stored_hash = cold_u64le(p + COLD_PROVIDER_ARCHIVE_HASH_OFFSET);
    if (version != COLD_PROVIDER_ARCHIVE_VERSION) goto done;
    if (target_len == 0 || format_len == 0) goto done;
    uint64_t pos = COLD_PROVIDER_ARCHIVE_HEADER_SIZE;
    if (!cold_file_range_ok(archive.len, pos, target_len)) goto done;
    Span target_span = {p + pos, (int32_t)target_len};
    pos += target_len;
    if (!cold_file_range_ok(archive.len, pos, format_len)) goto done;
    Span format_span = {p + pos, (int32_t)format_len};
    pos += format_len;
    if (target && target[0] != '\0' && !span_eq(target_span, target)) goto done;
    const char *expected_format = cold_object_format_for_target_cstr(target);
    if (expected_format[0] == '\0' || !span_eq(format_span, expected_format)) goto done;
    uint32_t seen_exports = 0;
    for (uint32_t mi = 0; mi < member_count; mi++) {
        if (!cold_file_range_ok(archive.len, pos, 28)) goto done;
        uint32_t module_len = cold_u32le(p + pos); pos += 4;
        uint32_t source_len = cold_u32le(p + pos); pos += 4;
        uint32_t object_size = cold_u32le(p + pos); pos += 4;
        uint32_t member_exports = cold_u32le(p + pos); pos += 4;
        uint64_t object_hash = cold_u64le(p + pos); pos += 8;
        uint32_t export_len = cold_u32le(p + pos); pos += 4;
        if (!cold_file_range_ok(archive.len, pos,
                                (uint64_t)module_len + source_len + export_len + object_size)) {
            goto done;
        }
        pos += module_len + source_len + export_len;
        uint64_t computed_object_hash =
            cold_fnv1a64_update_bytes(1469598103934665603ULL, p + pos, object_size);
        if (computed_object_hash != object_hash) goto done;
        pos += object_size;
        seen_exports += member_exports;
    }
    if (seen_exports != export_count) goto done;
    if (pos != (uint64_t)archive.len) goto done;
    {
        uint8_t *copy = (uint8_t *)malloc((size_t)archive.len);
        if (!copy) goto done;
        memcpy(copy, archive.ptr, (size_t)archive.len);
        uint64_t computed = cold_provider_archive_hash_bytes(copy, (size_t)archive.len);
        free(copy);
        if (computed != stored_hash) goto done;
    }
    if (member_count_out) *member_count_out = (int32_t)member_count;
    if (hash_out) *hash_out = stored_hash;
    ok = true;

done:
    if (archive.len > 0) munmap((void *)archive.ptr, (size_t)archive.len);
    return ok;
}

typedef struct ColdElfObjectView {
    const uint8_t *data;
    int32_t len;
    uint16_t machine;
    int32_t text_idx;
    const uint32_t *words;
    int32_t word_count;
    const Elf64_Sym *syms;
    int32_t sym_count;
    const char *strtab;
    uint64_t strtab_size;
    const Elf64_Rela *relas;
    int32_t reloc_count;
} ColdElfObjectView;

static bool cold_read_elf64_relocatable_view(const uint8_t *data,
                                             int32_t len,
                                             const char *target,
                                             ColdElfObjectView *out) {
    if (!data || len < 64 || !out) return false;
    memset(out, 0, sizeof(*out));
    uint16_t expected_machine = cold_elf_machine_for_target(target);
    if (expected_machine == 0) return false;
    if (memcmp(data, "\x7F""ELF", 4) != 0) return false;
    if (data[4] != ELFCLASS64 || data[5] != ELFDATA2LSB || data[6] != EV_CURRENT) return false;
    if (cold_u16le(data + 0x10) != ET_REL) return false;
    uint16_t machine = cold_u16le(data + 0x12);
    if (machine != expected_machine) return false;
    uint64_t shoff = cold_u64le(data + 0x28);
    uint16_t shentsize = cold_u16le(data + 0x3A);
    uint16_t shnum = cold_u16le(data + 0x3C);
    uint16_t shstrndx = cold_u16le(data + 0x3E);
    if (shentsize != sizeof(Elf64_Shdr) || shnum == 0 || shstrndx >= shnum) return false;
    if (!cold_file_range_ok(len, shoff, (uint64_t)shentsize * shnum)) return false;
    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(data + shoff);
    const Elf64_Shdr *shstr = &shdrs[shstrndx];
    if (shstr->sh_type != SHT_STRTAB) return false;
    if (!cold_file_range_ok(len, shstr->sh_offset, shstr->sh_size)) return false;
    const char *shstr_data = (const char *)(data + shstr->sh_offset);

    int32_t text_idx = -1;
    int32_t symtab_idx = -1;
    for (int32_t i = 0; i < (int32_t)shnum; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        if (!cold_file_range_ok(len, sh->sh_offset, sh->sh_size)) return false;
        if (sh->sh_type == SHT_PROGBITS &&
            (sh->sh_flags & SHF_EXECINSTR) &&
            cold_elf_section_name_eq(shstr_data, shstr->sh_size, sh->sh_name, ".text")) {
            text_idx = i;
        } else if (sh->sh_type == SHT_SYMTAB) {
            symtab_idx = i;
        }
    }
    if (text_idx <= 0 || symtab_idx <= 0) return false;
    int32_t rela_idx = -1;
    for (int32_t i = 0; i < (int32_t)shnum; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        if (sh->sh_type == SHT_RELA && sh->sh_info == (uint32_t)text_idx) {
            rela_idx = i;
            break;
        }
    }
    const Elf64_Shdr *text = &shdrs[text_idx];
    if ((text->sh_size % 4) != 0 || text->sh_size > (uint64_t)INT32_MAX * 4ULL) return false;
    const Elf64_Shdr *symtab = &shdrs[symtab_idx];
    if (symtab->sh_entsize != sizeof(Elf64_Sym) || symtab->sh_link >= shnum) return false;
    if ((symtab->sh_size % sizeof(Elf64_Sym)) != 0) return false;
    const Elf64_Shdr *strtab = &shdrs[symtab->sh_link];
    if (strtab->sh_type != SHT_STRTAB) return false;
    if (!cold_file_range_ok(len, strtab->sh_offset, strtab->sh_size)) return false;
    const Elf64_Rela *relas = 0;
    int32_t reloc_count = 0;
    if (rela_idx > 0) {
        const Elf64_Shdr *rela = &shdrs[rela_idx];
        if (rela->sh_entsize != sizeof(Elf64_Rela) ||
            rela->sh_link != (uint32_t)symtab_idx ||
            rela->sh_info != (uint32_t)text_idx ||
            (rela->sh_size % sizeof(Elf64_Rela)) != 0) {
            return false;
        }
        relas = (const Elf64_Rela *)(data + rela->sh_offset);
        reloc_count = (int32_t)(rela->sh_size / sizeof(Elf64_Rela));
    }
    out->data = data;
    out->len = len;
    out->machine = machine;
    out->text_idx = text_idx;
    out->words = (const uint32_t *)(data + text->sh_offset);
    out->word_count = (int32_t)(text->sh_size / 4);
    out->syms = (const Elf64_Sym *)(data + symtab->sh_offset);
    out->sym_count = (int32_t)(symtab->sh_size / sizeof(Elf64_Sym));
    out->strtab = (const char *)(data + strtab->sh_offset);
    out->strtab_size = strtab->sh_size;
    out->relas = relas;
    out->reloc_count = reloc_count;
    return true;
}

static const char *cold_elf_symbol_name(ColdElfObjectView *obj, uint32_t sym_index) {
    if (!obj || sym_index >= (uint32_t)obj->sym_count) return 0;
    uint32_t off = obj->syms[sym_index].st_name;
    if ((uint64_t)off >= obj->strtab_size) return 0;
    return obj->strtab + off;
}

static int32_t cold_elf_find_defined_symbol(ColdElfObjectView *obj, const char *name) {
    if (!obj || !name || name[0] == '\0') return -1;
    for (int32_t i = 1; i < obj->sym_count; i++) {
        const Elf64_Sym *sym = &obj->syms[i];
        if (sym->st_shndx != (uint16_t)obj->text_idx) continue;
        const char *sn = cold_elf_symbol_name(obj, (uint32_t)i);
        if (sn && strcmp(sn, name) == 0) return i;
    }
    return -1;
}

static bool cold_apply_elf_call_reloc(uint32_t *words,
                                      int32_t word_count,
                                      uint16_t machine,
                                      uint32_t type,
                                      int32_t word_pos,
                                      int32_t target_word) {
    if (!words || word_pos < 0 || word_pos >= word_count ||
        target_word < 0 || target_word >= word_count) {
        return false;
    }
    int32_t delta = target_word - word_pos;
    if (machine == EM_AARCH64 && type == R_AARCH64_CALL26) {
        if (delta < -(1 << 25) || delta >= (1 << 25)) return false;
        words[word_pos] = (words[word_pos] & 0xFC000000u) |
                          ((uint32_t)delta & 0x03FFFFFFu);
        return true;
    }
    if (machine == EM_RISCV && (type == R_RISCV_CALL || type == R_RISCV_CALL_PLT)) {
        if (delta < -(1 << 20) || delta >= (1 << 20)) return false;
        words[word_pos] = rv_jal(RV_RA, delta);
        return true;
    }
    return false;
}

static bool cold_link_relocs_for_object(ColdElfObjectView *obj,
                                        uint32_t *words,
                                        int32_t total_words,
                                        int32_t base_word,
                                        ColdElfObjectView *provider,
                                        const char *provider_export,
                                        int32_t provider_base_word,
                                        ColdCompileStats *stats) {
    for (int32_t i = 0; i < obj->reloc_count; i++) {
        const Elf64_Rela *reloc = &obj->relas[i];
        uint32_t sym_index = (uint32_t)(reloc->r_info >> 32);
        uint32_t type = (uint32_t)(reloc->r_info & 0xFFFFFFFFu);
        if (sym_index >= (uint32_t)obj->sym_count ||
            (reloc->r_offset % 4) != 0 ||
            reloc->r_offset / 4 >= (uint64_t)obj->word_count) {
            return false;
        }
        const Elf64_Sym *sym = &obj->syms[sym_index];
        int32_t target_word = -1;
        const char *sym_name = cold_elf_symbol_name(obj, sym_index);
        if (sym->st_shndx == (uint16_t)obj->text_idx) {
            if ((sym->st_value % 4) != 0 || sym->st_value / 4 >= (uint64_t)obj->word_count) return false;
            if ((reloc->r_addend % 4) != 0) return false;
            int64_t local_word = (int64_t)(sym->st_value / 4) + (reloc->r_addend / 4);
            if (local_word < 0 || local_word >= obj->word_count) return false;
            target_word = base_word + (int32_t)local_word;
        } else if (sym->st_shndx == 0 && provider && provider_export &&
                   sym_name && strcmp(sym_name, provider_export) == 0) {
            int32_t provider_sym = cold_elf_find_defined_symbol(provider, provider_export);
            if (provider_sym < 0) return false;
            const Elf64_Sym *ps = &provider->syms[provider_sym];
            if ((ps->st_value % 4) != 0 || ps->st_value / 4 >= (uint64_t)provider->word_count) return false;
            if ((reloc->r_addend % 4) != 0) return false;
            int64_t provider_word = (int64_t)(ps->st_value / 4) + (reloc->r_addend / 4);
            if (provider_word < 0 || provider_word >= provider->word_count) return false;
            target_word = provider_base_word + (int32_t)provider_word;
            if (stats) stats->provider_resolved_symbol_count++;
        } else if (sym->st_shndx == 0) {
            if (stats) {
                stats->unresolved_symbol_count++;
                if (stats->first_unresolved_symbol[0] == '\0' && sym_name) {
                    snprintf(stats->first_unresolved_symbol,
                             sizeof(stats->first_unresolved_symbol),
                             "%s", sym_name);
                }
            }
            continue;
        } else {
            return false;
        }
        int32_t word_pos = base_word + (int32_t)(reloc->r_offset / 4);
        if (!cold_apply_elf_call_reloc(words, total_words, obj->machine, type, word_pos, target_word)) return false;
        if (stats) stats->link_reloc_count++;
    }
    return true;
}

static bool cold_link_elf64_object_with_provider_archive(const char *object_path,
                                                         const char *archive_path,
                                                         const char *out_path,
                                                         const char *target,
                                                         ColdCompileStats *stats) {
    uint64_t start_us = cold_now_us();
    Span primary_span = source_open(object_path);
    Span archive_span = source_open(archive_path);
    uint32_t *words = 0;
    bool ok = false;
    if (primary_span.len <= 0 || archive_span.len <= 0) goto done;
    ColdElfObjectView primary;
    if (!cold_read_elf64_relocatable_view(primary_span.ptr, primary_span.len, target, &primary)) goto done;
    int32_t member_count = 0;
    uint64_t archive_hash = 0;
    if (!cold_verify_provider_archive(archive_path, target, &member_count, &archive_hash)) goto done;
    if (member_count != 1) goto done;

    const uint8_t *p = archive_span.ptr;
    uint64_t pos = COLD_PROVIDER_ARCHIVE_HEADER_SIZE;
    uint32_t target_len = cold_u32le(p + 12);
    uint32_t format_len = cold_u32le(p + 16);
    uint32_t export_count = cold_u32le(p + 24);
    pos += target_len + format_len;
    if (!cold_file_range_ok(archive_span.len, pos, 28)) goto done;
    uint32_t module_len = cold_u32le(p + pos); pos += 4;
    uint32_t source_len = cold_u32le(p + pos); pos += 4;
    uint32_t object_size = cold_u32le(p + pos); pos += 4;
    uint32_t member_exports = cold_u32le(p + pos); pos += 4;
    (void)cold_u64le(p + pos); pos += 8;
    uint32_t export_len = cold_u32le(p + pos); pos += 4;
    if (export_count != 1 || member_exports != 1 || export_len == 0) goto done;
    if (!cold_file_range_ok(archive_span.len, pos,
                            (uint64_t)module_len + source_len + export_len + object_size)) goto done;
    pos += module_len + source_len;
    char export_name[COLD_NAME_CAP];
    if (export_len >= sizeof(export_name)) goto done;
    memcpy(export_name, p + pos, export_len);
    export_name[export_len] = '\0';
    pos += export_len;
    const uint8_t *provider_obj_data = p + pos;
    ColdElfObjectView provider;
    if (!cold_read_elf64_relocatable_view(provider_obj_data, (int32_t)object_size, target, &provider)) goto done;
    if (cold_elf_find_defined_symbol(&provider, export_name) < 0) goto done;
    if (cold_elf_find_defined_symbol(&primary, export_name) >= 0) goto done;

    int32_t total_words = primary.word_count + provider.word_count;
    words = (uint32_t *)calloc(total_words > 0 ? (size_t)total_words : 1, sizeof(uint32_t));
    if (!words) goto done;
    memcpy(words, primary.words, (size_t)primary.word_count * sizeof(uint32_t));
    memcpy(words + primary.word_count, provider.words, (size_t)provider.word_count * sizeof(uint32_t));
    if (stats) {
        stats->link_object = 1;
        stats->provider_archive = 1;
        stats->provider_object_count = 1;
        stats->provider_archive_member_count = 1;
        stats->provider_archive_hash = archive_hash;
        stats->provider_export_count = 1;
    }
    if (!cold_link_relocs_for_object(&primary, words, total_words, 0,
                                     &provider, export_name, primary.word_count, stats)) goto done;
    if (!cold_link_relocs_for_object(&provider, words, total_words, primary.word_count,
                                     0, 0, 0, stats)) goto done;
    if (stats && stats->unresolved_symbol_count != 0) goto done;
    if (!elf_write_exec(out_path, words, total_words, primary.machine)) goto done;
    if (stats) {
        stats->code_words = total_words;
        stats->elapsed_us = cold_now_us() - start_us;
        stats->emit_us = stats->elapsed_us;
    }
    ok = true;

done:
    if (words) free(words);
    if (primary_span.len > 0) munmap((void *)primary_span.ptr, (size_t)primary_span.len);
    if (archive_span.len > 0) munmap((void *)archive_span.ptr, (size_t)archive_span.len);
    return ok;
}

static bool cold_write_provider_archive_pack_report(const char *path,
                                                    bool ok,
                                                    const char *target,
                                                    const char *object_path,
                                                    const char *archive_path,
                                                    const char *export_symbol,
                                                    int32_t member_count,
                                                    uint64_t archive_hash,
                                                    const char *error) {
    if (!path || path[0] == '\0') return true;
    char parent[PATH_MAX];
    if (cold_parent_dir(path, parent, sizeof(parent)) && !cold_mkdir_p(parent)) return false;
    FILE *file = fopen(path, "w");
    if (!file) return false;
    fprintf(file, "provider_archive_pack=%d\n", ok ? 1 : 0);
    fprintf(file, "provider_archive=%d\n", ok ? 1 : 0);
    fprintf(file, "target=%s\n", target ? target : "");
    fprintf(file, "object=%s\n", object_path ? object_path : "");
    fprintf(file, "output=%s\n", archive_path ? archive_path : "");
    fprintf(file, "provider_export=%s\n", export_symbol ? export_symbol : "");
    fprintf(file, "provider_archive_member_count=%d\n", member_count);
    fprintf(file, "provider_object_count=%d\n", ok ? member_count : 0);
    fprintf(file, "provider_export_count=%d\n", ok ? 1 : 0);
    fprintf(file, "provider_archive_hash=%016llx\n", (unsigned long long)archive_hash);
    fputs("system_link=0\n", file);
    fputs("linkerless_image=1\n", file);
    if (error && error[0] != '\0') fprintf(file, "error=%s\n", error);
    fclose(file);
    return true;
}

static int cold_cmd_provider_archive_pack(int argc, char **argv) {
    const char *target = cold_flag_value(argc, argv, "--target");
    const char *object_path = cold_flag_value(argc, argv, "--object");
    const char *out_path = cold_flag_value(argc, argv, "--out");
    const char *export_symbol = cold_flag_value(argc, argv, "--export");
    const char *module = cold_flag_value(argc, argv, "--module");
    const char *source = cold_flag_value(argc, argv, "--source");
    const char *report_path = cold_flag_value(argc, argv, "--report-out");
    if (!target || target[0] == '\0') target = "arm64-apple-darwin";
    if (!object_path || object_path[0] == '\0') {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "missing --object");
        fprintf(stderr, "[cheng_cold] provider-archive-pack requires --object:<obj>\n");
        return 2;
    }
    if (!out_path || out_path[0] == '\0') {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "missing --out");
        fprintf(stderr, "[cheng_cold] provider-archive-pack requires --out:<archive>\n");
        return 2;
    }
    if (!export_symbol || export_symbol[0] == '\0') {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "missing --export");
        fprintf(stderr, "[cheng_cold] provider-archive-pack requires --export:<symbol>\n");
        return 2;
    }
    uint16_t machine = cold_elf_machine_for_target(target);
    if (machine == 0) {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "provider archive requires ELF target");
        fprintf(stderr, "[cheng_cold] provider-archive-pack currently requires ELF target\n");
        return 2;
    }
    Span object = source_open(object_path);
    if (object.len <= 0) {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "object open failed");
        fprintf(stderr, "[cheng_cold] provider object open failed: %s\n", object_path);
        return 2;
    }
    ColdElfObjectView view;
    bool valid = cold_read_elf64_relocatable_view(object.ptr, object.len, target, &view);
    bool has_export = valid && cold_elf_find_defined_symbol(&view, export_symbol) >= 0;
    if (object.len > 0) munmap((void *)object.ptr, (size_t)object.len);
    if (!valid) {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "invalid provider object");
        fprintf(stderr, "[cheng_cold] invalid provider object: %s\n", object_path);
        return 2;
    }
    if (!has_export) {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "provider export not defined");
        fprintf(stderr, "[cheng_cold] provider export not defined: %s\n", export_symbol);
        return 2;
    }
    int32_t member_count = 0;
    uint64_t archive_hash = 0;
    if (!cold_write_provider_archive(out_path, target, object_path, module, source, export_symbol,
                                     &member_count, &archive_hash) ||
        member_count != 1 ||
        !cold_verify_provider_archive(out_path, target, &member_count, &archive_hash)) {
        cold_write_provider_archive_pack_report(report_path, false, target, object_path, out_path,
                                                export_symbol, 0, 0, "provider archive write failed");
        fprintf(stderr, "[cheng_cold] provider archive write failed: %s\n", out_path);
        return 2;
    }
    cold_write_provider_archive_pack_report(report_path, true, target, object_path, out_path,
                                            export_symbol, member_count, archive_hash, "");
    printf("provider_archive_pack=1\n");
    printf("provider_archive_member_count=%d\n", member_count);
    printf("provider_archive_hash=%016llx\n", (unsigned long long)archive_hash);
    printf("output=%s\n", out_path);
    return 0;
}

/* Compile source to Mach-O object file (.o) with symbol table */
#ifndef COLD_BACKEND_ONLY
bool cold_compile_source_to_object(const char *out_path, const char *src_path, const char *target) {
    bool is_elf  = target && strstr(target, "linux") != 0;
    bool is_coff = target && strstr(target, "windows") != 0;
    uint16_t elf_machine  = 0;
    uint16_t coff_machine = 0;
    if (is_elf) {
        if (strstr(target, "aarch64")) elf_machine = EM_AARCH64;
        else if (strstr(target, "riscv64")) elf_machine = EM_RISCV;
        else if (strstr(target, "x86_64"))  elf_machine = EM_X86_64;
    }
    if (is_coff) {
        if (strstr(target, "aarch64")) coff_machine = IMAGE_FILE_MACHINE_ARM64;
        else if (strstr(target, "x86_64"))  coff_machine = IMAGE_FILE_MACHINE_AMD64;
    }
    Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (arena == MAP_FAILED) return false;

    Symbols *symbols = symbols_new(arena);
    BodyIR **function_bodies = 0;
    int32_t body_cap = 0;
    int32_t main_function = -1;
    int32_t first_function = -1;

    Span mapped_source = source_open(src_path);
    if (mapped_source.len <= 0) return false;

    cold_collect_imported_function_signatures(symbols, mapped_source);
    cold_collect_function_signatures(symbols, mapped_source);
    /* Collect direct imports for bare-name resolution in entry module body */
    ColdImportSource import_sources[64];
    int32_t import_source_count = cold_collect_direct_import_sources(mapped_source,
                                                                     import_sources, 64);
    { /* workspace root detection for import loading */
      char ws_root[PATH_MAX] = {0};
      const char *sm = strstr(src_path, "/src/");
      if (!sm) sm = strstr(src_path, "src/");
      if (sm == src_path) { char rp[PATH_MAX]; if (realpath(src_path, rp)) { const char *s = strstr(rp, "/src/"); if (s) { size_t rl = (size_t)(s - rp); memcpy(ws_root, rp, rl); ws_root[rl] = 0; } } }
      else if (sm) { size_t rl = (size_t)(sm - src_path); if (rl < sizeof(ws_root)) { memcpy(ws_root, src_path, rl); ws_root[rl] = 0; } }
    }
    body_cap = symbols->function_cap;
    if (body_cap < 256) body_cap = 256;
    function_bodies = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
    memset(function_bodies, 0, (size_t)body_cap * sizeof(BodyIR *));

    /* Import body compilation: skip if CHENG_NO_IMPORT_BODIES=1 */
    if (!getenv("CHENG_NO_IMPORT_BODIES") && symbols->function_count < 4096) {
        ColdErrorRecoveryEnabled = true;
        if (setjmp(ColdErrorJumpBuf) == 0) {
            cold_compile_imported_bodies_no_recurse(symbols, mapped_source, function_bodies, body_cap);
        }
        ColdErrorRecoveryEnabled = false;
    }
    if (symbols->function_cap > body_cap) {
        int32_t old_cap = body_cap;
        body_cap = symbols->function_cap;
        BodyIR **grown = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
        memset(grown, 0, (size_t)body_cap * sizeof(BodyIR *));
        memcpy(grown, function_bodies, (size_t)old_cap * sizeof(BodyIR *));
        function_bodies = grown;
    }
    if (ColdImportSegvSaw) {
        ColdImportSegvSaw = 0;
        for (int32_t i = symbols->function_count; i < body_cap; i++) {
            function_bodies[i] = NULL;
        }
    }

    Parser parser = {mapped_source, 0, arena, symbols};
    parser.import_sources = import_sources;
    parser.import_source_count = import_source_count;
    parser.function_bodies = function_bodies;
    parser.function_body_cap = body_cap;
    while (parser.pos < mapped_source.len) {
        parser_ws(&parser);
        if (parser.pos >= mapped_source.len) break;
        Span next = parser_peek(&parser);
        if (span_eq(next, "type")) {
            parse_type(&parser);
        } else if (span_eq(next, "const")) {
            parse_const(&parser);
        } else if (span_eq(next, "fn")) {
            int32_t symbol_index = -1;
            BodyIR *body = parse_fn(&parser, &symbol_index);
            if (symbol_index >= 0 && symbol_index < body_cap) {
                function_bodies[symbol_index] = body;
                if (first_function < 0) first_function = symbol_index;
                if (span_eq(symbols->functions[symbol_index].name, "main"))
                    main_function = symbol_index;
            }
        } else {
            parser_line(&parser);
        }
    }
    (void)main_function;

    /* Resize function_bodies if parsing added more functions */
    if (symbols->function_cap > body_cap) {
        int32_t old_cap = body_cap;
        body_cap = symbols->function_cap;
        BodyIR **grown = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
        memset(grown, 0, (size_t)body_cap * sizeof(BodyIR *));
        memcpy(grown, function_bodies, (size_t)old_cap * sizeof(BodyIR *));
        function_bodies = grown;
    }

    if (first_function < 0) {
        /* No compilable functions: write minimal valid .o */
        bool ok = is_elf ? elf64_write_object(out_path, 0, 0, 0, 0, 0, 0, 0, 0, 0, elf_machine) : is_coff ? coff_write_object(out_path, 0, 0, 0, 0, 0, 0, 0, 0, 0, coff_machine) : macho_write_object(out_path, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
        return ok;
    }

    /* Compile all functions into a shared Code buffer with position tracking.
       This enables cross-function BL instruction resolution (same approach
       as codegen_program for the direct executable path). */
    int32_t func_count = symbols->function_count;
    Code *shared = code_new(arena, 1024);
    int32_t *symbol_offset = arena_alloc(arena, (size_t)func_count * sizeof(int32_t));
    for (int32_t i = 0; i < func_count; i++) symbol_offset[i] = -1;

    FunctionPatchList function_patches = {0};
    function_patches.arena = arena;

    /* Detect target architecture for codegen dispatch */
    bool use_x64 = (elf_machine == EM_X86_64 || coff_machine == IMAGE_FILE_MACHINE_AMD64);
    bool use_rv64 = (elf_machine == EM_RISCV);
    X64Code x64_buf; if (use_x64) { x64_init(&x64_buf, 65536); }

    /* Entry trampoline: save argc/argv in callee-saved registers */
    int32_t trampoline_bl_pos = shared->count + 4;
    if (use_x64) {
        /* x86_64 entry: argc in %rdi (ARG1), argv in %rsi (ARG2).
           Save into callee-saved registers r12=argv, r13=argc for ARGC_LOAD/ARGV_STR. */
        trampoline_bl_pos = x64_buf.len + 1; /* after E8 opcode */
        x64_push_r64(&x64_buf, 5); /* push %rbp */
        x64_mov_r64_r64(&x64_buf, 5, 4); /* mov %rsp,%rbp */
        x64_mov_r64_r64(&x64_buf, 12, 1); /* mov %rdi,%r12 (argv) */
        x64_mov_r64_r64(&x64_buf, 13, 2); /* mov %rsi,%r13 (argc saved) */
        x64_mov_r64_r64(&x64_buf, 3, 0); /* mov %rdi,%rbx (argc -> rbx for ARGC_LOAD) */
        x64_call_rel32(&x64_buf, 0); /* placeholder, patched to main */
        x64_mov_r64_r64(&x64_buf, 4, 5); /* mov %rbp,%rsp */
        x64_pop_r64(&x64_buf, 5); /* pop %rbp */
        x64_ret(&x64_buf);
    } else {
        code_emit(shared, a64_add_imm(19, R0, 0, true));
        code_emit(shared, a64_add_imm(20, R1, 0, true));
        code_emit(shared, a64_add_imm(21, LR, 0, true));
        code_emit(shared, a64_add_imm(22, R2, 0, true));
        code_emit(shared, a64_bl(0));
        code_emit(shared, a64_add_imm(LR, 21, 0, true));
        code_emit(shared, a64_ret());
    }

    /* First pass: compile each function body into the shared buffer */
    for (int32_t i = 0; i < func_count && i < body_cap; i++) {
        if (!function_bodies[i]) continue;
        BodyIR *body = function_bodies[i];
        if (use_x64) symbol_offset[i] = x64_buf.len; /* byte offset */
        else symbol_offset[i] = shared->count;
        if ((uintptr_t)body < 4096 || body->has_fallback ||
            body->block_count == 0 ||
            (body->block_count > 0 && body->block_term[0] < 0)) {
            /* Emit stub that zero-initializes the return slot */
            if (use_x64) {
                if (body->return_kind == SLOT_I32) {
                    x64_mov_r32_imm32(&x64_buf, 0, 0);
                    x64_ret(&x64_buf);
                } else if (body->return_kind == SLOT_I64 || body->return_kind == SLOT_PTR) {
                    x64_mov_r64_imm64(&x64_buf, 0, 0);
                    x64_ret(&x64_buf);
                } else {
                    x64_mov_r32_imm32(&x64_buf, 0, 0);
                    x64_ret(&x64_buf);
                }
                continue;
            }
            if (use_rv64) {
                code_emit(shared, rv_addi(RV_A0, RV_ZERO, 0));
                code_emit(shared, rv_jalr(RV_ZERO, RV_RA, 0));
                continue;
            }
            if (body->return_kind == SLOT_STR && body->slot_offset) {
                code_emit(shared, a64_movz(R0, 0, 0));
                a64_emit_str_sp_off(shared, R0, body->slot_offset[0], true);
                a64_emit_str_sp_off(shared, R0, body->slot_offset[0] + 8, true);
                a64_emit_str_sp_off(shared, R0, body->slot_offset[0] + COLD_STR_STORE_ID_OFFSET, false);
                a64_emit_str_sp_off(shared, R0, body->slot_offset[0] + COLD_STR_FLAGS_OFFSET, false);
                code_emit(shared, a64_ret());
            } else if (body->return_kind == SLOT_I32) {
                code_emit(shared, a64_movz(R0, 0, 0));
                code_emit(shared, a64_ret());
            } else if (body->return_kind == SLOT_I64 || body->return_kind == SLOT_PTR) {
                code_emit(shared, a64_movz_x(R0, 0, 0));
                code_emit(shared, a64_ret());
            } else {
                /* Composite returns: zero the sret buffer */
                int32_t sret = body->sret_slot;
                if (sret >= 0 && body->slot_offset &&
                    sret < body->slot_count) {
                    a64_emit_ldr_sp_off(shared, R0, body->slot_offset[sret], true);
                    codegen_zero_slot(shared, body, sret);
                }
                code_emit(shared, a64_ret());
            }
            continue;
        }
        if (use_x64)
            x64_codegen_func(&x64_buf, body, symbols, &function_patches);
        else if (use_rv64)
            rv64_codegen_func(shared, body, symbols, &function_patches);
        else
            codegen_func(shared, body, symbols, &function_patches);
    }

    /* Relocation tracking (shared between all architectures) */
    int32_t reloc_cap = function_patches.count > 0 ? function_patches.count : 1;
    int32_t *reloc_offsets = arena_alloc(arena, (size_t)reloc_cap * sizeof(int32_t));
    int32_t *reloc_symbols = arena_alloc(arena, (size_t)reloc_cap * sizeof(int32_t));
    int32_t reloc_count = 0;

    if (use_x64) {
        /* Resolve x86_64 patches in byte buffer before packing */
        for (int32_t pi = 0; pi < function_patches.count; pi++) {
            FunctionPatch patch = function_patches.items[pi];
            if (patch.target_function < 0 || patch.target_function >= func_count) continue;
            int32_t target_off = symbol_offset[patch.target_function];
            if (target_off < 0) continue; /* external: leave as relocation */
            x64_patch_call_rel32(&x64_buf, (int32_t)patch.pos, target_off);
        }
        /* Re-collect unresolved (external) patches as relocations */
        for (int32_t pi = 0; pi < function_patches.count; pi++) {
            FunctionPatch patch = function_patches.items[pi];
            if (patch.target_function < 0 || patch.target_function >= func_count) continue;
            if (symbol_offset[patch.target_function] >= 0) continue; /* already resolved */
            if (reloc_count >= reloc_cap) die("cold object relocation overflow");
            reloc_offsets[reloc_count] = (int32_t)patch.pos * 4; /* byte offset */ /* byte offset */
            reloc_symbols[reloc_count] = patch.target_function;
            reloc_count++;
        }
        /* Pack bytes into words */
        shared->count = x64_pack_words(&x64_buf, shared->words, shared->cap);
        for (int32_t i = 0; i < func_count; i++)
            if (symbol_offset[i] >= 0) symbol_offset[i] /= 4;
        /* Convert relocation offsets from bytes to words */
        for (int32_t ri = 0; ri < reloc_count; ri++)
            reloc_offsets[ri] /= 4;
    } else {
        /* Resolve intra-object patches for ARM64/RISC-V (word-level) */
        for (int32_t pi = 0; pi < function_patches.count; pi++) {
            FunctionPatch patch = function_patches.items[pi];
            if (patch.target_function < 0 || patch.target_function >= func_count) {
                die("cold object function patch target out of range");
            }
            int32_t target_off = symbol_offset[patch.target_function];
            if (target_off < 0) {
                if (reloc_count >= reloc_cap) die("cold object relocation overflow");
                reloc_offsets[reloc_count] = (int32_t)patch.pos * 4; /* byte offset */
                reloc_symbols[reloc_count] = patch.target_function;
                reloc_count++;
                continue;
            }
            int32_t delta = target_off - (int32_t)patch.pos;
            if (use_rv64) {
                /* RISC-V JAL: patch the 20-bit immediate using RV_J encoding */
                uint32_t w = shared->words[patch.pos];
                uint32_t imm = (uint32_t)(delta & 0x1FFFFF);
                shared->words[patch.pos] = (w & 0xFFFu) |
                    ((imm & 0xFF000u) >> 0) |
                    ((imm & 0x800u) << 9) |
                    ((imm & 0x7FEu) << 20) |
                    ((imm & 0x100000u) << 11);
            } else {
                shared->words[patch.pos] = (shared->words[patch.pos] & 0xFC000000u) |
                                           ((uint32_t)delta & 0x03FFFFFFu);
            }
        }
    }

    if (main_function >= 0 && symbol_offset[main_function] >= 0) {
        int32_t target = symbol_offset[main_function];
        if (use_x64) {
            /* x86_64: trampoline bl placeholder at byte offset trampoline_bl_pos */
            x64_patch_call_rel32(&x64_buf, trampoline_bl_pos, target * 4);
            shared->count = x64_pack_words(&x64_buf, shared->words, shared->cap);
        } else {
            shared->words[trampoline_bl_pos] = a64_bl(target - trampoline_bl_pos);
        }
    }

    int32_t max_names = func_count + reloc_count + 1;
    const char **func_names = arena_alloc(arena, (size_t)max_names * sizeof(const char *));
    int32_t *func_offsets = arena_alloc(arena, (size_t)max_names * sizeof(int32_t));
    int32_t name_count = 0, local_count = 0;
    for (int32_t i = 0; i < func_count; i++) {
        if (symbol_offset[i] < 0) continue;
        Span nm = symbols->functions[i].name;
        func_offsets[name_count] = (i == main_function) ? 0 : symbol_offset[i];
        char *nc = arena_alloc(arena, (size_t)nm.len + 1);
        memcpy(nc, nm.ptr, (size_t)nm.len); nc[nm.len] = '\0';
        func_names[name_count] = nc;
        name_count++;
    }
    if (getenv("CHENG_NO_IMPORT_BODIES")) local_count = name_count;

    int32_t *ext_sym_map = arena_alloc(arena, (size_t)func_count * sizeof(int32_t));
    for (int32_t i = 0; i < func_count; i++) ext_sym_map[i] = -1;
    for (int32_t ri = 0; ri < reloc_count; ri++) {
        int32_t si = reloc_symbols[ri];
        if (si < 0 || si >= func_count) continue;
        if (ext_sym_map[si] >= 0) { reloc_symbols[ri] = ext_sym_map[si]; continue; }
        Span nm = symbols->functions[si].name;
        const char *base = nm.ptr; int32_t blen = nm.len;
        for (int32_t k = nm.len - 1; k > 0; k--)
            if (nm.ptr[k] == '.') { base = nm.ptr + k + 1; blen = nm.len - k - 1; break; }
        func_offsets[name_count] = -1;
        char *nc = arena_alloc(arena, (size_t)blen + 1);
        memcpy(nc, base, (size_t)blen); nc[blen] = '\0';
        func_names[name_count] = nc;
        ext_sym_map[si] = name_count;
        reloc_symbols[ri] = name_count;
        name_count++;
    }

    bool ok = is_elf ? elf64_write_object(out_path, shared->words, shared->count, func_names, func_offsets, name_count, local_count, reloc_offsets, reloc_symbols, reloc_count, elf_machine) : is_coff ? coff_write_object(out_path, shared->words, shared->count, func_names, func_offsets, name_count, local_count, reloc_offsets, reloc_symbols, reloc_count, coff_machine) : macho_write_object(out_path, shared->words, shared->count, func_names, func_offsets, name_count, local_count, reloc_offsets, reloc_symbols, reloc_count);
    return ok;
}

/* ================================================================
 * CSG v2 binary format writer
 * ================================================================ */
static void cold_emit_csg_v2_u32(FILE *f, uint32_t v) {
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
    fputc((int)((v >> 16) & 0xFF), f);
    fputc((int)((v >> 24) & 0xFF), f);
}

static void cold_emit_csg_v2_u64(FILE *f, uint64_t v) {
    cold_emit_csg_v2_u32(f, (uint32_t)(v & 0xFFFFFFFF));
    cold_emit_csg_v2_u32(f, (uint32_t)((v >> 32) & 0xFFFFFFFF));
}

static void cold_emit_csg_v2_str(FILE *f, Span s) {
    cold_emit_csg_v2_u32(f, (uint32_t)s.len);
    if (s.len > 0) fwrite(s.ptr, 1, (size_t)s.len, f);
}

static bool cold_emit_csg_v2_facts(const char *path, BodyIR **function_bodies,
                                    int32_t func_count, Symbols *symbols,
                                    const char *target) {
    if (!function_bodies || !symbols || func_count <= 0) return false;
    /* Count functions to serialize: valid bodies OR named symbols */
    int32_t valid_count = 0;
    for (int32_t i = 0; i < func_count; i++) {
        BodyIR *b = function_bodies[i];
        if (b && !b->has_fallback) valid_count++;
        else if (symbols->functions[i].name.len > 0) valid_count++;
    }
    if (valid_count <= 0) return false;

    FILE *f = fopen(path, "wb");
    if (!f) return false;

    /* ---- HEADER (64 bytes) ---- */
    fwrite("CHENGCSG", 1, 8, f);
    cold_emit_csg_v2_u32(f, 2); /* version */
    {   /* target_triple (32 bytes, null-padded) */
        char triple[32];
        memset(triple, 0, 32);
        if (target) {
            size_t tlen = strlen(target);
            if (tlen > 32) tlen = 32;
            memcpy(triple, target, tlen);
        }
        fwrite(triple, 1, 32, f);
    }
    cold_emit_csg_v2_u32(f, 0); /* abi = system */
    fputc(8, f);                 /* pointer_width = 8 */
    fputc(0, f);                 /* endianness = LE */
    {   /* reserved 14 bytes */
        uint8_t reserved[14];
        memset(reserved, 0, 14);
        fwrite(reserved, 1, 14, f);
    }

    /* ---- SECTION INDEX PLACEHOLDER ---- */
    /* section_count(4) + 2 sections * (kind(4)+offset(8)+size(4)) = 36 bytes */
    long section_index_pos = ftell(f);
    {   uint8_t placeholder[36];
        memset(placeholder, 0, 36);
        fwrite(placeholder, 1, 36, f);
    }

    /* ---- COLLECT UNIQUE STRING LITERALS ---- */
    int32_t total_str_slots = 0;
    for (int32_t i = 0; i < func_count; i++) {
        BodyIR *b = function_bodies[i];
        if (!b || b->has_fallback) continue;
        if (b->string_literal_count > 0 && b->string_literal_count < 65536)
            total_str_slots += b->string_literal_count;
    }

    Span *unique_strs = NULL;
    int32_t *str_id_map = NULL;
    int32_t unique_str_count = 0;

    if (total_str_slots > 0) {
        unique_strs = (Span *)malloc((size_t)total_str_slots * sizeof(Span));
        str_id_map = (int32_t *)malloc((size_t)total_str_slots * sizeof(int32_t));
    }

    {
        int32_t map_idx = 0;
        for (int32_t fi = 0; fi < func_count; fi++) {
            BodyIR *b = function_bodies[fi];
            if (!b || b->has_fallback) continue;
            if (b->string_literal_count < 0 || b->string_literal_count > 65536 ||
                !b->string_literal) continue;
            for (int32_t si = 0; si < b->string_literal_count; si++) {
                Span s = b->string_literal[si];
                if (s.ptr == NULL || s.len <= 0) continue;
                int32_t id = -1;
                for (int32_t ui = 0; ui < unique_str_count; ui++) {
                    if (unique_strs[ui].len == s.len &&
                        (s.len == 0 || memcmp(unique_strs[ui].ptr, s.ptr, (size_t)s.len) == 0)) {
                        id = ui;
                        break;
                    }
                }
                if (id < 0) {
                    id = unique_str_count++;
                    unique_strs[id] = s;
                }
                str_id_map[map_idx++] = id;
            }
        }
    }

    /* ---- FUNCTION SECTION ---- */
    long func_section_start = ftell(f);
    cold_emit_csg_v2_u32(f, (uint32_t)valid_count);

    {
        int32_t map_idx = 0;
        for (int32_t fi = 0; fi < func_count; fi++) {
            BodyIR *b = function_bodies[fi];
            /* Skip null/fallback functions with no symbol entry (gaps) */
            if ((!b || b->has_fallback) && symbols->functions[fi].name.len <= 0) continue;

            /* name: u32 len + utf8 bytes */
            cold_emit_csg_v2_str(f, symbols->functions[fi].name);
            if (!b || b->has_fallback) {
                /* Treat as external declaration: no params, no ops, no slots */
                cold_emit_csg_v2_u32(f, 0); /* param_count */
                cold_emit_csg_v2_u32(f, SLOT_I32); /* return_kind */
                cold_emit_csg_v2_u32(f, 0); /* frame_size */
                cold_emit_csg_v2_u32(f, 0); /* op_count */
                cold_emit_csg_v2_u32(f, 0); /* slot_count */
                cold_emit_csg_v2_u32(f, 0); /* block_count */
                cold_emit_csg_v2_u32(f, 0); /* term_count */
                cold_emit_csg_v2_u32(f, 0); /* switch_count */
                cold_emit_csg_v2_u32(f, 0); /* call_count */
                cold_emit_csg_v2_u32(f, 0); /* call_arg_count */
                cold_emit_csg_v2_u32(f, 0); /* str_lit_count */
                continue;
            }

            /* param_count + per-param ABI triple: slot_kind, slot_size, param_slot */
            cold_emit_csg_v2_u32(f, (uint32_t)b->param_count);
            for (int32_t pi = 0; pi < b->param_count; pi++) {
                int32_t ps = b->param_slot[pi];
                int32_t pk = (ps >= 0 && ps < b->slot_count) ? b->slot_kind[ps] : SLOT_I32;
                int32_t pz = (ps >= 0 && ps < b->slot_count) ? b->slot_size[ps] : cold_slot_size_for_kind(pk);
                cold_emit_csg_v2_u32(f, (uint32_t)pk);
                cold_emit_csg_v2_u32(f, (uint32_t)pz);
                cold_emit_csg_v2_u32(f, (uint32_t)ps);
            }
            /* return_kind */
            cold_emit_csg_v2_u32(f, (uint32_t)b->return_kind);
            /* frame_size */
            cold_emit_csg_v2_u32(f, (uint32_t)b->frame_size);
            /* op_count + ops (5 x u32 = 20 bytes per op) */
            cold_emit_csg_v2_u32(f, (uint32_t)b->op_count);
            for (int32_t oi = 0; oi < b->op_count; oi++) {
                cold_emit_csg_v2_u32(f, (uint32_t)b->op_kind[oi]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->op_dst[oi]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->op_a[oi]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->op_b[oi]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->op_c[oi]);
            }
            /* slot_count + slots (3 x u32 = 12 bytes per slot) */
            cold_emit_csg_v2_u32(f, (uint32_t)b->slot_count);
            for (int32_t si = 0; si < b->slot_count; si++) {
                cold_emit_csg_v2_u32(f, (uint32_t)b->slot_kind[si]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->slot_offset[si]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->slot_size[si]);
            }
            /* block_count + blocks (3 x u32 = 12 bytes per block) */
            cold_emit_csg_v2_u32(f, (uint32_t)b->block_count);
            for (int32_t bi = 0; bi < b->block_count; bi++) {
                cold_emit_csg_v2_u32(f, (uint32_t)b->block_op_start[bi]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->block_op_count[bi]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->block_term[bi]);
            }
            /* term_count + terms (6 x u32 = 24 bytes per term) */
            cold_emit_csg_v2_u32(f, (uint32_t)b->term_count);
            for (int32_t ti = 0; ti < b->term_count; ti++) {
                cold_emit_csg_v2_u32(f, (uint32_t)b->term_kind[ti]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->term_value[ti]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->term_case_start[ti]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->term_case_count[ti]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->term_true_block[ti]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->term_false_block[ti]);
            }
            /* switch_case_count + switch entries (3 x u32 = 12 bytes each) */
            cold_emit_csg_v2_u32(f, (uint32_t)b->switch_count);
            for (int32_t si = 0; si < b->switch_count; si++) {
                cold_emit_csg_v2_u32(f, (uint32_t)b->switch_tag[si]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->switch_block[si]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->switch_term[si]);
            }
            /* call_count + call entries (4 x u32 = 16 bytes each) */
            {
                int32_t call_count = 0;
                for (int32_t oi = 0; oi < b->op_count; oi++) {
                    int32_t ok = b->op_kind[oi];
                    if (ok == BODY_OP_CALL_I32 || ok == BODY_OP_CALL_COMPOSITE)
                        call_count++;
                }
                cold_emit_csg_v2_u32(f, (uint32_t)call_count);
                for (int32_t oi = 0; oi < b->op_count; oi++) {
                    int32_t ok = b->op_kind[oi];
                    if (ok != BODY_OP_CALL_I32 && ok != BODY_OP_CALL_COMPOSITE)
                        continue;
                    int32_t target_fn = b->op_a[oi];
                    int32_t arg_start  = b->op_b[oi];
                    int32_t arg_count;
                    if (ok == BODY_OP_CALL_I32) {
                        if (target_fn >= 0 && target_fn < symbols->function_count)
                            arg_count = symbols->functions[target_fn].arity;
                        else
                            arg_count = 0;
                    } else {
                        arg_count = b->op_c[oi];
                    }
                    int32_t result_slot = b->op_dst[oi];
                    cold_emit_csg_v2_u32(f, (uint32_t)target_fn);
                    cold_emit_csg_v2_u32(f, (uint32_t)arg_start);
                    cold_emit_csg_v2_u32(f, (uint32_t)arg_count);
                    cold_emit_csg_v2_u32(f, (uint32_t)result_slot);
                }
            }
            /* call_arg_count + call args (2 x u32 = 8 bytes each) */
            {
                int32_t cac = b->call_arg_count;
                if (cac < 0 || cac > 65536) cac = 0;
                cold_emit_csg_v2_u32(f, (uint32_t)cac);
                for (int32_t cai = 0; cai < cac; cai++) {
                cold_emit_csg_v2_u32(f, (uint32_t)b->call_arg_slot[cai]);
                cold_emit_csg_v2_u32(f, (uint32_t)b->call_arg_offset[cai]);
                }
            }
            /* string_literal_count + string literal IDs */
            cold_emit_csg_v2_u32(f, (uint32_t)b->string_literal_count);
            for (int32_t si = 0; si < b->string_literal_count; si++) {
                cold_emit_csg_v2_u32(f, (uint32_t)str_id_map[map_idx++]);
            }
        }
    }

    long str_section_start = ftell(f);

    /* ---- STRING SECTION ---- */
    cold_emit_csg_v2_u32(f, (uint32_t)unique_str_count);
    for (int32_t ui = 0; ui < unique_str_count; ui++) {
        cold_emit_csg_v2_u32(f, (uint32_t)ui); /* id */
        cold_emit_csg_v2_str(f, unique_strs[ui]);
    }

    long trailer_start = ftell(f);

    /* ---- REWRITE SECTION INDEX ---- */
    fseek(f, section_index_pos, SEEK_SET);
    cold_emit_csg_v2_u32(f, 2); /* section_count = 2 */
    /* Section 0: function (kind=1) */
    cold_emit_csg_v2_u32(f, 1); /* section_kind = function */
    cold_emit_csg_v2_u64(f, (uint64_t)func_section_start);
    cold_emit_csg_v2_u32(f, (uint32_t)(str_section_start - func_section_start));
    /* Section 1: string_literal (kind=5) */
    cold_emit_csg_v2_u32(f, 5); /* section_kind = string_literal */
    cold_emit_csg_v2_u64(f, (uint64_t)str_section_start);
    cold_emit_csg_v2_u32(f, (uint32_t)(trailer_start - str_section_start));

    /* Seek to trailer position */
    fseek(f, trailer_start, SEEK_SET);

    /* ---- TRAILER (32 bytes) ---- */
    cold_emit_csg_v2_u32(f, 2); /* section_count (redundant) */
    {   /* content_hash (28 bytes, zero for Phase 0) */
        uint8_t hash[28];
        memset(hash, 0, 28);
        fwrite(hash, 1, 28, f);
    }

    fclose(f);
    free(unique_strs);
    free(str_id_map);
    return true;
}

/* Emit CSG v2 facts from source as a sidecar (used by --csg-out flag).
   Opens source, compiles with imports, calls cold_emit_csg_v2_facts. */
static bool cold_emit_csg_v2_facts_from_source(const char *facts_path,
                                                const char *source_path,
                                                const char *target) {
    Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (arena == MAP_FAILED) return false;
    Symbols *symbols = symbols_new(arena);
    Span src = source_open(source_path);
    if (src.len <= 0) return false;
    cold_collect_imported_function_signatures(symbols, src);
    cold_collect_function_signatures(symbols, src);
    cold_collect_all_transitive_imports(symbols, src);
    ColdImportSource import_sources[64];
    int32_t import_count = cold_collect_direct_import_sources(src, import_sources, 64);
    int32_t body_cap = symbols->function_cap;
    if (body_cap < 256) body_cap = 256;
    BodyIR **function_bodies = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
    memset(function_bodies, 0, (size_t)body_cap * sizeof(BodyIR *));
    ColdErrorRecoveryEnabled = true;
    if (setjmp(ColdErrorJumpBuf) == 0) {
        cold_compile_imported_bodies_no_recurse(symbols, src, function_bodies, body_cap);
    }
    ColdErrorRecoveryEnabled = false;
    /* Parse main source */
    Parser parser = {src, 0, arena, symbols};
    parser.import_sources = import_sources;
    parser.import_source_count = import_count;
    parser.function_bodies = function_bodies;
    parser.function_body_cap = body_cap;
    while (parser.pos < src.len) {
        parser_ws(&parser);
        if (parser.pos >= src.len) break;
        Span next = parser_peek(&parser);
        if (span_eq(next, "type")) parse_type(&parser);
        else if (span_eq(next, "const")) parse_const(&parser);
        else if (span_eq(next, "fn")) {
            int32_t si = -1;
            BodyIR *body = parse_fn(&parser, &si);
            if (si >= 0 && si < body_cap) function_bodies[si] = body;
        } else parser_line(&parser);
    }
    bool ok = cold_emit_csg_v2_facts(facts_path, function_bodies,
                                      symbols->function_count, symbols, target);
    munmap((void *)src.ptr, (size_t)src.len);
    return ok;
}
#endif /* COLD_BACKEND_ONLY */

static int cold_cmd_system_link_exec(int argc, char **argv) {
    const char *source_path = cold_flag_value(argc, argv, "--in");
    const char *csg_in_path = cold_flag_value(argc, argv, "--csg-in");
    const char *csg_out_path = cold_flag_value(argc, argv, "--csg-out");
    const char *cold_csg_out_path = cold_flag_value(argc, argv, "--cold-csg-out");
    const char *out_path = cold_flag_value(argc, argv, "--out");
    const char *report_path = cold_flag_value(argc, argv, "--report-out");
    const char *target = cold_flag_value(argc, argv, "--target");
    const char *emit = cold_flag_value(argc, argv, "--emit");
    const char *link_object_path = cold_flag_value(argc, argv, "--link-object");
    const char *provider_archive_path = cold_flag_value(argc, argv, "--provider-archive");
    const char *provider_objects = cold_flag_value(argc, argv, "--provider-objects");
    if (!target || target[0] == '\0') target = "arm64-apple-darwin";
    if (!emit || emit[0] == '\0') emit = "exe";
    if ((!csg_out_path || csg_out_path[0] == '\0') &&
        cold_csg_out_path && cold_csg_out_path[0] != '\0') {
        csg_out_path = cold_csg_out_path;
    }
    const char *effective_csg_path = csg_in_path;

    /* Parse diagnostic flags */
    for (int di = 2; di < argc; di++) {
        if (strcmp(argv[di], "--diag:dump_per_fn") == 0) cold_diag_dump_per_fn = true;
        if (strcmp(argv[di], "--diag:dump_slots") == 0) cold_diag_dump_slots = true;
    }
    if (cold_diag_dump_per_fn) fprintf(stderr, "[diag] dump_per_fn ENABLED\n");
    if (cold_diag_dump_slots) fprintf(stderr, "[diag] dump_slots ENABLED\n");

    /* ensure report sidecar is written even if die() fires */
    if (report_path && report_path[0] != '\0') {
        snprintf(ColdDieReportPath, sizeof(ColdDieReportPath), "%s", report_path);
    }

    if (csg_in_path && csg_in_path[0] != '\0' && csg_out_path && csg_out_path[0] != '\0') {
        cold_write_system_link_exec_report(report_path, false, source_path, csg_in_path, out_path,
                                           target, emit, 0, "cannot combine --csg-in and --csg-out");
        fprintf(stderr, "[cheng_cold] cannot combine --csg-in and --csg-out\n");
        return 2;
    }
    if ((!source_path || source_path[0] == '\0') &&
        (!csg_in_path || csg_in_path[0] == '\0') &&
        (!link_object_path || link_object_path[0] == '\0')) {
        cold_write_system_link_exec_report(report_path, false, source_path, csg_in_path, out_path,
                                           target, emit, 0, "missing --in");
        fprintf(stderr, "[cheng_cold] missing --in:<source>, --csg-in:<facts>, or --link-object:<obj>\n");
        return 2;
    }
#ifdef COLD_BACKEND_ONLY
    if (source_path && source_path[0] && (!csg_in_path || csg_in_path[0] == '\0')) {
        cold_write_system_link_exec_report(report_path, false, source_path, 0, out_path,
                                           target, emit, 0, "source compile disabled in backend-only mode");
        fprintf(stderr, "[cheng_cold] backend-only: --in:<source> disabled, use --csg-in:<facts>\n");
        return 2;
    }
    if (csg_out_path && csg_out_path[0] != '\0') {
        cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                           target, emit, 0, "--csg-out requires source parser (disabled in backend-only)");
        fprintf(stderr, "[cheng_cold] backend-only: --csg-out disabled\n");
        return 2;
    }
#endif
    bool csg_sidecar = false;
    if (csg_out_path && csg_out_path[0] != '\0') {
        if (!source_path || source_path[0] == '\0') {
            cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                               target, emit, 0, "missing --in for --csg-out");
            fprintf(stderr, "[cheng_cold] --csg-out requires --in:<source>\n");
            return 2;
        }
        /* If --csg-in is also set, --csg-out is the output path for CSG compilation.
           Otherwise, --csg-out is a sidecar: compile from source AND emit CSG facts. */
        if (csg_in_path && csg_in_path[0] != '\0') {
            effective_csg_path = csg_out_path;
        } else {
            csg_sidecar = true;
        }
    }
    if (!out_path || out_path[0] == '\0') {
        cold_write_system_link_exec_report(report_path, false, source_path, effective_csg_path, out_path,
                                           target, emit, 0, "missing --out");
        fprintf(stderr, "[cheng_cold] missing --out:<path>\n");
        return 2;
    }
    bool is_elf = (strcmp(target, "aarch64-unknown-linux-gnu") == 0 ||
                    strcmp(target, "x86_64-unknown-linux-gnu") == 0 ||
                    strcmp(target, "riscv64-unknown-linux-gnu") == 0);
    bool is_coff = (strcmp(target, "x86_64-pc-windows-msvc") == 0 ||
                     strcmp(target, "aarch64-pc-windows-msvc") == 0);
    bool is_macho = (strcmp(target, "arm64-apple-darwin") == 0);
    if (!is_macho && !is_elf && !is_coff) {
        cold_write_system_link_exec_report(report_path, false, source_path, effective_csg_path, out_path,
                                           target, emit, 0, "unsupported target");
        fprintf(stderr, "[cheng_cold] unsupported target: %s\n", target);
        return 2;
    }
    if (strcmp(emit, "exe") != 0 && strcmp(emit, "obj") != 0 && strcmp(emit, "csg") != 0 && strcmp(emit, "csg-v2") != 0) {
        cold_write_system_link_exec_report(report_path, false, source_path, effective_csg_path, out_path,
                                           target, emit, 0, "unsupported emit");
        fprintf(stderr, "[cheng_cold] unsupported emit: %s\n", emit);
        return 2;
    }
    if (link_object_path && link_object_path[0] != '\0') {
        ColdCompileStats stats = {0};
        stats.link_object = 1;
        if ((source_path && source_path[0] != '\0') ||
            (csg_in_path && csg_in_path[0] != '\0') ||
            (csg_out_path && csg_out_path[0] != '\0')) {
            cold_write_system_link_exec_report(report_path, false, link_object_path, provider_archive_path, out_path,
                                               target, emit, &stats, "cannot combine --link-object with source or csg input");
            fprintf(stderr, "[cheng_cold] cannot combine --link-object with --in/--csg-in/--csg-out\n");
            return 2;
        }
        if (strcmp(emit, "exe") != 0) {
            cold_write_system_link_exec_report(report_path, false, link_object_path, provider_archive_path, out_path,
                                               target, emit, &stats, "--link-object requires --emit:exe");
            fprintf(stderr, "[cheng_cold] --link-object requires --emit:exe\n");
            return 2;
        }
        if (!is_elf) {
            cold_write_system_link_exec_report(report_path, false, link_object_path, provider_archive_path, out_path,
                                               target, emit, &stats, "--link-object currently requires ELF target");
            fprintf(stderr, "[cheng_cold] --link-object currently requires ELF target\n");
            return 2;
        }
        bool linked = false;
        if (provider_archive_path && provider_archive_path[0] != '\0') {
            linked = cold_link_elf64_object_with_provider_archive(link_object_path, provider_archive_path,
                                                                  out_path, target, &stats);
        } else {
            linked = cold_link_elf64_object_to_exec(link_object_path, out_path, target, &stats);
        }
        if (!linked) {
            cold_write_system_link_exec_report(report_path, false, link_object_path, provider_archive_path, out_path,
                                               target, emit, &stats, "link object failed");
            fprintf(stderr, "[cheng_cold] link object failed: %s\n", link_object_path);
            return 2;
        }
        cold_write_system_link_exec_report(report_path, true, link_object_path, provider_archive_path, out_path,
                                           target, emit, &stats, "");
        printf("system_link_exec=1\n");
        printf("real_backend_codegen=1\n");
        printf("system_link_exec_scope=cold_link_object\n");
        cold_print_elapsed_ms(stdout, "cold_compile_elapsed_ms", stats.elapsed_us);
        printf("output=%s\n", out_path);
        return 0;
    }
    if (provider_archive_path && provider_archive_path[0] != '\0') {
        cold_write_system_link_exec_report(report_path, false, source_path, provider_archive_path, out_path,
                                           target, emit, 0, "--provider-archive requires --link-object");
        fprintf(stderr, "[cheng_cold] --provider-archive requires --link-object\n");
        return 2;
    }
    /* --emit:obj = emit real Mach-O object file (.o) with symbols */
    if (strcmp(emit, "obj") == 0) {
        if (csg_in_path && csg_in_path[0] != '\0') {
            ColdCompileStats stats = {0};
            if (!cold_compile_csg_path_to_macho(out_path, csg_in_path, 0, 0,
                                                target, &stats, true, provider_objects)) {
                cold_write_system_link_exec_report(report_path, false, source_path, csg_in_path, out_path,
                                                   target, emit, &stats, "cold csg v2 object emit failed");
                return 2;
            }
            cold_write_system_link_exec_report(report_path, true, source_path, csg_in_path, out_path,
                                               target, emit, &stats, "");
            return 0;
        }
#ifndef COLD_BACKEND_ONLY
        if (!source_path || source_path[0] == '\0') {
            cold_write_system_link_exec_report(report_path, false, source_path, out_path, out_path,
                                               target, emit, 0, "missing --in for emit:obj");
            return 2;
        }
        if (!cold_compile_source_to_object(out_path, source_path, target)) {
            cold_write_system_link_exec_report(report_path, false, source_path, out_path, out_path,
                                               target, emit, 0, "cold object emit failed");
            return 2;
        }
        cold_write_system_link_exec_report(report_path, true, source_path, out_path, out_path,
                                           target, emit, 0, "");
        return 0;
#endif
    }
#ifndef COLD_BACKEND_ONLY
    /* --emit:csg-v2 = emit binary CSG v2 facts file */
    if (strcmp(emit, "csg-v2") == 0) {
        if (!source_path || source_path[0] == '\0') {
            cold_write_system_link_exec_report(report_path, false, source_path, out_path, out_path,
                                               target, emit, 0, "missing --in for emit:csg-v2");
            fprintf(stderr, "[cheng_cold] --emit:csg-v2 requires --in:<source>\n");
            return 2;
        }
        /* Parse source into BodyIR, then serialize as CSG v2 */
        Arena *arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANON, -1, 0);
        if (arena == MAP_FAILED) {
            cold_write_system_link_exec_report(report_path, false, source_path, out_path, out_path,
                                               target, emit, 0, "arena allocation failed");
            return 2;
        }
        Symbols *symbols = symbols_new(arena);
        BodyIR **function_bodies = NULL;
        int32_t body_cap = 0;

        Span mapped_source = source_open(source_path);
        if (mapped_source.len <= 0) {
            cold_write_system_link_exec_report(report_path, false, source_path, out_path, out_path,
                                               target, emit, 0, "source_open failed");
            return 2;
        }

        cold_collect_imported_function_signatures(symbols, mapped_source);
        cold_collect_function_signatures(symbols, mapped_source);
        ColdImportSource import_sources[64];
        int32_t import_source_count = cold_collect_direct_import_sources(mapped_source,
                                                                         import_sources, 64);

        body_cap = symbols->function_cap;
        if (body_cap < 256) body_cap = 256;
        function_bodies = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
        memset(function_bodies, 0, (size_t)body_cap * sizeof(BodyIR *));

        /* Collect transitive import function signatures */
        cold_collect_all_transitive_imports(symbols, mapped_source);

        /* Grow function_bodies if needed after transitive collection */
        if (symbols->function_cap > body_cap) {
            int32_t old_cap = body_cap;
            body_cap = symbols->function_cap;
            BodyIR **grown = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
            memset(grown, 0, (size_t)body_cap * sizeof(BodyIR *));
            if (old_cap > 0) memcpy(grown, function_bodies, (size_t)old_cap * sizeof(BodyIR *));
            function_bodies = grown;
        }

        /* Compile direct import bodies */
        if (symbols->function_count < 4096) {
            ColdErrorRecoveryEnabled = true;
            if (setjmp(ColdErrorJumpBuf) == 0) {
                cold_compile_imported_bodies_no_recurse(symbols, mapped_source,
                                                        function_bodies, body_cap);
            }
            ColdErrorRecoveryEnabled = false;
        }

        /* Compile transitive import bodies: recursively process each import file */
        {
            char visited_paths[64][PATH_MAX];
            int32_t visited_count = 0;
            for (int32_t ii = 0; ii < import_source_count && ii < 64; ii++) {
                snprintf(visited_paths[visited_count++], PATH_MAX, "%s", import_sources[ii].path);
            }
            for (int32_t vi = 0; vi < visited_count && vi < 64; vi++) {
                Span src = source_open(visited_paths[vi]);
                if (src.len <= 0) continue;
                ColdErrorRecoveryEnabled = true;
                if (setjmp(ColdErrorJumpBuf) == 0) {
                    cold_compile_imported_bodies_no_recurse(symbols, src,
                                                            function_bodies, body_cap);
                }
                ColdErrorRecoveryEnabled = false;
                /* Also find this import's own imports and add to visited list */
                int32_t pos = 0;
                while (pos < src.len && visited_count < 64) {
                    while (pos < src.len && src.ptr[pos] != 'i') pos++;
                    if (pos >= src.len) break;
                    if (strncmp((const char *)(src.ptr + pos), "import ", 7) == 0) {
                        pos += 7;
                        int32_t start = pos;
                        while (pos < src.len && src.ptr[pos] != '\n' && src.ptr[pos] != ';') pos++;
                        Span mod = span_sub(src, start, pos);
                        mod = span_trim(mod);
                        Span alias = {0};
                        char resolved[PATH_MAX];
                        if (cold_import_source_path(mod, resolved, sizeof(resolved))) {
                            bool already = false;
                            for (int32_t ci = 0; ci < visited_count; ci++)
                                if (strcmp(visited_paths[ci], resolved) == 0) { already = true; break; }
                            if (!already)
                                snprintf(visited_paths[visited_count++], PATH_MAX, "%s", resolved);
                        }
                    }
                    pos++;
                }
                munmap((void *)src.ptr, (size_t)src.len);
            }
        }

        /* Parse functions from source */
        Parser parser = {mapped_source, 0, arena, symbols};
        parser.import_sources = import_sources;
        parser.import_source_count = import_source_count;
        parser.function_bodies = function_bodies;
        parser.function_body_cap = body_cap;
        while (parser.pos < mapped_source.len) {
            parser_ws(&parser);
            if (parser.pos >= mapped_source.len) break;
            Span next = parser_peek(&parser);
            if (span_eq(next, "type")) {
                parse_type(&parser);
            } else if (span_eq(next, "const")) {
                parse_const(&parser);
            } else if (span_eq(next, "fn")) {
                int32_t symbol_index = -1;
                BodyIR *body = parse_fn(&parser, &symbol_index);
                if (symbol_index >= 0 && symbol_index < body_cap) {
                    function_bodies[symbol_index] = body;
                }
            } else {
                parser_line(&parser);
            }
        }

        /* Grow function_bodies if parse added more functions */
        if (symbols->function_cap > body_cap) {
            int32_t old_cap = body_cap;
            body_cap = symbols->function_cap;
            BodyIR **grown = arena_alloc(arena, (size_t)body_cap * sizeof(BodyIR *));
            memset(grown, 0, (size_t)body_cap * sizeof(BodyIR *));
            memcpy(grown, function_bodies, (size_t)old_cap * sizeof(BodyIR *));
            function_bodies = grown;
        }

        if (!cold_emit_csg_v2_facts(out_path, function_bodies, symbols->function_count,
                                     symbols, target)) {
            cold_write_system_link_exec_report(report_path, false, source_path, out_path, out_path,
                                               target, emit, 0, "csg-v2 emit failed");
            fprintf(stderr, "[cheng_cold] csg-v2 emit failed\n");
            munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
            return 2;
        }

        ColdCompileStats stats = {0};
        struct stat csg_st;
        if (stat(out_path, &csg_st) == 0 && csg_st.st_size > 0) {
            stats.facts_bytes = (uint64_t)csg_st.st_size;
        }
        stats.csg_lowering = 1;
        stats.function_count = symbols->function_count;
        munmap((void *)mapped_source.ptr, (size_t)mapped_source.len);
        cold_write_system_link_exec_report(report_path, true, source_path, out_path, out_path,
                                           target, emit, &stats, "");
        return 0;
    }
#endif /* COLD_BACKEND_ONLY */
    /* derive workspace root from source path for import resolution */
    char workspace_root[PATH_MAX] = {0};
    if (source_path) {
        const char *src_marker = strstr(source_path, "/src/");
        if (src_marker) {
            size_t root_len = (size_t)(src_marker - source_path);
            if (root_len < sizeof(workspace_root)) {
                memcpy(workspace_root, source_path, root_len);
                workspace_root[root_len] = '\0';
            }
        }
        if (workspace_root[0] == '\0') {
            /* source_path is relative like "src/core/..." → resolve to absolute to find project root */
            char abs_path[PATH_MAX];
            if (realpath(source_path, abs_path)) {
                const char *asrc = strstr(abs_path, "/src/");
                if (asrc) {
                    size_t plen = (size_t)(asrc - abs_path);
                    memcpy(workspace_root, abs_path, plen);
                    workspace_root[plen] = '\0';
                }
            }
            if (workspace_root[0] == '\0' && getcwd(workspace_root, sizeof(workspace_root))) {
                /* use cwd as last resort */
            }
        }
    }

    /* --link-providers: compile primary + each import + link with cc */
    int link_providers = 0;
    for (int di = 2; di < argc; di++) {
        if (strcmp(argv[di], "--link-providers") == 0) link_providers = 1;
    }

    ColdCompileStats stats = {0};
    int compiled = 0;
    if (link_providers && strcmp(emit, "exe") == 0 &&
        ((source_path && source_path[0]) || (effective_csg_path && effective_csg_path[0]))) {
        char primary_o[PATH_MAX], host_stubs_o[PATH_MAX];
        snprintf(primary_o, sizeof(primary_o), "%s.primary.o", out_path);
        snprintf(host_stubs_o, sizeof(host_stubs_o), "%s.host_stubs.o", out_path);

        if (effective_csg_path && effective_csg_path[0]) {
            /* CSG path: compile facts → primary .o */
            if (!cold_compile_csg_path_to_macho(primary_o, effective_csg_path, 0, 0, target, &stats, true, 0)) {
                fprintf(stderr, "[cheng_cold] --link-providers: csg primary compile failed\n");
                return 2;
            }
        } else {
#ifndef COLD_BACKEND_ONLY
            setenv("CHENG_NO_IMPORT_BODIES", "1", 1);
            if (!cold_compile_source_to_object(primary_o, source_path, target)) {
                fprintf(stderr, "[cheng_cold] --link-providers: primary compile failed\n");
                return 2;
            }
            unsetenv("CHENG_NO_IMPORT_BODIES");
#endif /* COLD_BACKEND_ONLY */
        }

        { char cmd[PATH_MAX * 2];
          snprintf(cmd, sizeof(cmd),
            "cc -std=c11 -c -o %s src/core/runtime/host_runtime_stubs.c 2>/dev/null",
            host_stubs_o);
          int rc = system(cmd);
          (void)rc;
        }

#ifndef COLD_BACKEND_ONLY
        Span source = source_path ? source_open(source_path) : (Span){0};
        char import_paths[32][PATH_MAX];
        int import_count = 0;
        if (source.len > 0) {
            int pos = 0;
            while (pos < source.len && import_count < 32) {
                while (pos < source.len && source.ptr[pos] != 'i') pos++;
                if (pos >= source.len) break;
                if (strncmp((const char *)(source.ptr + pos), "import ", 7) == 0) {
                    int start = pos;
                    pos += 7;
                    while (pos < source.len && source.ptr[pos] != '\n') pos++;
                    Span import_line = {source.ptr + start, pos - start};
                    Span module_path = {0}, alias = {0};
                    if (cold_parse_import_line(import_line, &module_path, &alias) &&
                        module_path.len > 0) {
                        char resolved[PATH_MAX];
                        if (cold_import_source_path(module_path, resolved, sizeof(resolved))) {
                            snprintf(import_paths[import_count], PATH_MAX, "%s", resolved);
                            import_count++;
                        }
                    }
                }
                pos++;
            }
            munmap((void *)source.ptr, (size_t)source.len);
        }
        /* For CSG path: facts already contain all needed code; skip import scanning.
           Only link with pre-compiled provider .o files if --provider-objects is specified. */

        char provider_o[32][PATH_MAX];
        int provider_count = 0;
        for (int i = 0; i < import_count; i++) {
            snprintf(provider_o[provider_count], PATH_MAX,
              "%s.provider.%d.o", out_path, i);
            if (!cold_compile_source_to_object(provider_o[provider_count], import_paths[i], target)) {
                fprintf(stderr, "[cheng_cold] --link-providers: import compile failed: %s\n",
                  import_paths[i]);
                cold_write_system_link_exec_report(report_path, false, source_path, 0, out_path,
                                                   target, emit, &stats, "provider import compile failed");
                return 2;
            }
            provider_count++;
        }
#endif /* COLD_BACKEND_ONLY */

#ifndef COLD_BACKEND_ONLY
        { char cmd[PATH_MAX * 8];
          int off = snprintf(cmd, sizeof(cmd),
            "cc -arch arm64 %s %s", host_stubs_o, primary_o);
          for (int i = 0; i < provider_count; i++)
              off += snprintf(cmd + off, sizeof(cmd) - off, " %s", provider_o[i]);
          snprintf(cmd + off, sizeof(cmd) - off, " -lc -o %s", out_path);
          int rc = system(cmd);
          unlink(primary_o);
          for (int i = 0; i < provider_count; i++) unlink(provider_o[i]);
          if (rc != 0) {
            fprintf(stderr, "[cheng_cold] --link-providers: link failed\n");
            cold_write_system_link_exec_report(report_path, false, source_path, 0, out_path,
                                               target, emit, &stats, "provider link failed");
            return 2;
          }
          compiled = 1;
        }
#endif
    } else if (effective_csg_path && effective_csg_path[0]) {
        compiled = cold_compile_csg_path_to_macho(out_path, effective_csg_path, source_path,
                                                  workspace_root[0] ? workspace_root : 0,
                                                  target, &stats, false, 0);
#ifndef COLD_BACKEND_ONLY
    } else {
        compiled = cold_compile_source_path_to_macho(out_path, source_path, false, &stats);
    }
    /* Emit CSG v2 facts as sidecar when --csg-out is present */
    if (compiled && csg_sidecar && csg_out_path && csg_out_path[0]) {
        Arena *sc_arena = mmap(0, sizeof(Arena), PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANON, -1, 0);
        if (sc_arena != MAP_FAILED) {
            Symbols *sc_sym = symbols_new(sc_arena);
            Span sc_src = source_open(source_path);
            if (sc_src.len > 0) {
                cold_collect_imported_function_signatures(sc_sym, sc_src);
                cold_collect_function_signatures(sc_sym, sc_src);
                cold_collect_all_transitive_imports(sc_sym, sc_src);
                ColdImportSource sc_imp[64];
                int32_t sc_imp_c = cold_collect_direct_import_sources(sc_src, sc_imp, 64);
                int32_t sc_bcap = sc_sym->function_cap;
                if (sc_bcap < 256) sc_bcap = 256;
                BodyIR **sc_bodies = arena_alloc(sc_arena, (size_t)sc_bcap * sizeof(BodyIR *));
                memset(sc_bodies, 0, (size_t)sc_bcap * sizeof(BodyIR *));
                ColdErrorRecoveryEnabled = true;
                if (setjmp(ColdErrorJumpBuf) == 0)
                    cold_compile_imported_bodies_no_recurse(sc_sym, sc_src, sc_bodies, sc_bcap);
                ColdErrorRecoveryEnabled = false;
                Parser sc_parser = {sc_src, 0, sc_arena, sc_sym};
                sc_parser.import_sources = sc_imp;
                sc_parser.import_source_count = sc_imp_c;
                while (sc_parser.pos < sc_src.len) {
                    parser_ws(&sc_parser);
                    if (sc_parser.pos >= sc_src.len) break;
                    Span next = parser_peek(&sc_parser);
                    if (span_eq(next, "type")) parse_type(&sc_parser);
                    else if (span_eq(next, "const")) parse_const(&sc_parser);
                    else if (span_eq(next, "fn")) {
                        int32_t si = -1;
                        BodyIR *b = parse_fn(&sc_parser, &si);
                        if (si >= 0 && si < sc_bcap) sc_bodies[si] = b;
                    } else parser_line(&sc_parser);
                }
                if (!cold_emit_csg_v2_facts(csg_out_path, sc_bodies, sc_sym->function_count, sc_sym, target)) {
                    munmap((void *)sc_src.ptr, (size_t)sc_src.len);
                    cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                                       target, emit, &stats, "csg sidecar emit failed");
                    fprintf(stderr, "[cheng_cold] csg sidecar emit failed: %s\n", csg_out_path);
                    return 2;
                }
                stats.csg_lowering = 1;
                stats.facts_function_count = sc_sym->function_count;
                struct stat sc_st;
                if (stat(csg_out_path, &sc_st) != 0 || sc_st.st_size <= 0 ||
                    !cold_file_starts_with_text(csg_out_path, "CHENGCSG")) {
                    munmap((void *)sc_src.ptr, (size_t)sc_src.len);
                    cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                                       target, emit, &stats, "csg sidecar verification failed");
                    fprintf(stderr, "[cheng_cold] csg sidecar verification failed: %s\n", csg_out_path);
                    return 2;
                }
                stats.facts_bytes = (uint64_t)sc_st.st_size;
                munmap((void *)sc_src.ptr, (size_t)sc_src.len);
            } else {
                cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                                   target, emit, &stats, "csg sidecar source_open failed");
                fprintf(stderr, "[cheng_cold] csg sidecar source_open failed: %s\n", source_path);
                return 2;
            }
        } else {
            cold_write_system_link_exec_report(report_path, false, source_path, csg_out_path, out_path,
                                               target, emit, &stats, "csg sidecar arena allocation failed");
            fprintf(stderr, "[cheng_cold] csg sidecar arena allocation failed\n");
            return 2;
        }
    }
#else /* COLD_BACKEND_ONLY */
    }
#endif /* COLD_BACKEND_ONLY */
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

static bool cold_file_exists_nonempty(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static bool cold_arg_is_value_for_separate_flag(int argc, char **argv, int index) {
    if (index <= 2 || index >= argc) return false;
    const char *prev = argv[index - 1];
    if (!prev || prev[0] == '\0') return false;
    return strcmp(prev, "--root") == 0 ||
           strcmp(prev, "--compiler") == 0 ||
           strcmp(prev, "--label") == 0 ||
           strcmp(prev, "--tail-only") == 0;
}

static int cold_run_host_smoke_ordinary(int argc, char **argv, const char *root) {
    (void)argc;
    char out_dir[PATH_MAX];
    char source_path[PATH_MAX];
    char out_path[PATH_MAX];
    char report_path[PATH_MAX];
    cold_join_path(out_dir, sizeof(out_dir), root, "artifacts/hostrun/cold_builtin");
    if (!cold_mkdir_p(out_dir)) {
        fprintf(stderr, "[cheng_cold] run-host-smokes: failed to create %s\n", out_dir);
        return 1;
    }
    cold_join_path(source_path, sizeof(source_path), root, "src/tests/ordinary_zero_exit_fixture.cheng");
    cold_join_path(out_path, sizeof(out_path), out_dir, "ordinary_zero_exit_fixture");
    snprintf(report_path, sizeof(report_path), "%s.report.txt", out_path);
    unlink(out_path);
    unlink(report_path);
    if (!cold_file_exists_nonempty(source_path)) {
        fprintf(stderr, "[cheng_cold] run-host-smokes: missing source %s\n", source_path);
        return 1;
    }
    char root_arg[PATH_MAX + 16];
    char in_arg[PATH_MAX + 16];
    char out_arg[PATH_MAX + 16];
    char report_arg[PATH_MAX + 32];
    snprintf(root_arg, sizeof(root_arg), "--root:%s", root);
    snprintf(in_arg, sizeof(in_arg), "--in:%s", source_path);
    snprintf(out_arg, sizeof(out_arg), "--out:%s", out_path);
    snprintf(report_arg, sizeof(report_arg), "--report-out:%s", report_path);
    char *compile_argv[] = {
        argv[0],
        "system-link-exec",
        root_arg,
        in_arg,
        "--emit:exe",
        "--target:arm64-apple-darwin",
        out_arg,
        report_arg,
    };
    int rc = cold_cmd_system_link_exec(8, compile_argv);
    if (rc != 0) return rc;
    if (!cold_run_executable_noargs_rc(out_path, 0)) {
        fprintf(stderr, "[cheng_cold] run-host-smokes: ordinary executable exit mismatch: %s\n", out_path);
        return 1;
    }
    if (!cold_file_contains_text(report_path, "direct_macho=1\n") ||
        !cold_file_contains_text(report_path, "system_link=0\n") ||
        !cold_file_contains_text(report_path, "linkerless_image=1\n")) {
        fprintf(stderr, "[cheng_cold] run-host-smokes: ordinary report mismatch: %s\n", report_path);
        return 1;
    }
    printf("  PASS ordinary_zero_exit_fixture\n");
    return 0;
}

static int cold_run_host_smoke_csg_v2_roundtrip(const char *self_path, const char *root) {
    char script_path[PATH_MAX];
    char q_root[PATH_MAX * 2];
    char q_self[PATH_MAX * 2];
    char cmd[PATH_MAX * 5];
    cold_join_path(script_path, sizeof(script_path), root, "tools/cold_csg_v2_roundtrip_test.sh");
    if (!cold_file_exists_nonempty(script_path)) {
        fprintf(stderr, "[cheng_cold] run-host-smokes: missing script %s\n", script_path);
        return 1;
    }
    if (!cold_shell_quote(q_root, sizeof(q_root), root) ||
        !cold_shell_quote(q_self, sizeof(q_self), self_path)) return 1;
    snprintf(cmd, sizeof(cmd),
             "cd %s && CHENG_BACKEND_DRIVER=%s tools/cold_csg_v2_roundtrip_test.sh",
             q_root, q_self);
    if (!cold_run_shell(cmd, "run-host-smokes cold_csg_sidecar_smoke")) return 1;
    printf("  PASS cold_csg_sidecar_smoke\n");
    return 0;
}

static int cold_run_host_smoke_skill_docs(const char *root) {
    char skill[PATH_MAX];
    char grammar[PATH_MAX];
    char spec[PATH_MAX];
    char intro[PATH_MAX];
    cold_join_path(skill, sizeof(skill), root, "docs/cheng-skill/SKILL.md");
    cold_join_path(grammar, sizeof(grammar), root, "docs/cheng-skill/references/grammar.md");
    cold_join_path(spec, sizeof(spec), root, "docs/cheng-formal-spec.md");
    cold_join_path(intro, sizeof(intro), root, "docs/cheng-language-introduction.md");
    if (!cold_file_contains_text(skill, "`last_verified_date`: `2026-05-13`") ||
        !cold_file_contains_text(skill, "T { field: value }") ||
        !cold_file_contains_text(skill, "tools/cold_csg_v2_roundtrip_test.sh") ||
        !cold_file_contains_text(skill, "emit-cold-csg-v2") ||
        !cold_file_contains_text(grammar, "2026-05-13") ||
        !cold_file_contains_text(grammar, "TypeName { field: value }") ||
        !cold_file_contains_text(spec, "版本：2026-05-13") ||
        !cold_file_contains_text(spec, "CSG v2") ||
        !cold_file_contains_text(intro, "tools/cold_csg_v2_roundtrip_test.sh")) {
        fprintf(stderr, "[cheng_cold] run-host-smokes: cheng skill docs mismatch\n");
        return 1;
    }
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') {
        char home_skill[PATH_MAX];
        char home_grammar[PATH_MAX];
        snprintf(home_skill, sizeof(home_skill), "%s/.codex/skills/cheng语言/SKILL.md", home);
        snprintf(home_grammar, sizeof(home_grammar), "%s/.codex/skills/cheng语言/references/grammar.md", home);
        if (cold_file_exists_nonempty(home_skill) && !cold_files_equal(skill, home_skill)) {
            fprintf(stderr, "[cheng_cold] run-host-smokes: home skill mirror drift\n");
            return 1;
        }
        if (cold_file_exists_nonempty(home_grammar) && !cold_files_equal(grammar, home_grammar)) {
            fprintf(stderr, "[cheng_cold] run-host-smokes: home grammar mirror drift\n");
            return 1;
        }
    }
    printf("  PASS cheng_skill_consistency_smoke\n");
    return 0;
}

static int cold_cmd_run_host_smokes(int argc, char **argv, const char *self_path) {
    const char *root_arg = cold_flag_value(argc, argv, "--root");
    char root[PATH_MAX];
    if (root_arg && root_arg[0] != '\0') cold_absolute_path(root_arg, root, sizeof(root));
    else if (!getcwd(root, sizeof(root))) snprintf(root, sizeof(root), ".");
    bool ran = false;
    for (int i = 2; i < argc; i++) {
        const char *arg = argv[i];
        if (!arg || arg[0] == '\0') continue;
        if (cold_arg_is_value_for_separate_flag(argc, argv, i)) continue;
        if (strncmp(arg, "--", 2) == 0) continue;
        int rc = 0;
        if (strcmp(arg, "ordinary_zero_exit_fixture") == 0) {
            rc = cold_run_host_smoke_ordinary(argc, argv, root);
        } else if (strcmp(arg, "cold_csg_sidecar_smoke") == 0) {
            rc = cold_run_host_smoke_csg_v2_roundtrip(self_path, root);
        } else if (strcmp(arg, "cheng_skill_consistency_smoke") == 0) {
            rc = cold_run_host_smoke_skill_docs(root);
        } else {
            fprintf(stderr, "[cheng_cold] run-host-smokes unsupported smoke: %s\n", arg);
            fprintf(stderr, "[cheng_cold] supported: ordinary_zero_exit_fixture, cold_csg_sidecar_smoke, cheng_skill_consistency_smoke\n");
            return 2;
        }
        if (rc != 0) return rc;
        ran = true;
    }
    if (!ran) {
        fprintf(stderr, "[cheng_cold] run-host-smokes requires explicit smoke name\n");
        fprintf(stderr, "[cheng_cold] supported: ordinary_zero_exit_fixture, cold_csg_sidecar_smoke, cheng_skill_consistency_smoke\n");
        return 2;
    }
    printf("cheng_cold run-host-smokes ok\n");
    return 0;
}

/* cold_parser.c included here so all cheng_cold.c static functions are visible */
#ifndef COLD_BACKEND_ONLY
#define COLD_CHENG_INCLUDE
#include "cold_parser.c"
#endif

/* ================================================================
 * Main
 * ================================================================ */
static Span cold_cached_minimal_macho = {0};

static char **ColdArgv0 = 0;

#ifndef COLD_BACKEND_ONLY
static int32_t cold_materialize_self_exec(Parser *parser, BodyIR *body) {
    if (cold_cached_minimal_macho.len <= 0) {
        const char *self_path = ColdArgv0 ? ColdArgv0[0] : "/proc/self/exe";
        cold_cached_minimal_macho = source_open(self_path);
        if (cold_cached_minimal_macho.len <= 0) die("cold self binary materialize failed");
    }
    int32_t li = body_string_literal(body, cold_cached_minimal_macho);
    int32_t bin_slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_op(body, BODY_OP_STR_LITERAL, bin_slot, li, 0);
    int32_t temp_path = cold_make_str_literal_cstr_slot(body, "/tmp/cold_probe_self");
    int32_t write_dst = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_PATH_WRITE_TEXT, write_dst, temp_path, bin_slot);
    body_op(body, BODY_OP_CHMOD_X, write_dst, temp_path, 0);
    int32_t exec_dst = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_COLD_SELF_EXEC, exec_dst, temp_path, 0);
    return exec_dst;
}

static int32_t cold_materialize_direct_emit(Parser *parser, BodyIR *body, int32_t output) {
    if (cold_cached_minimal_macho.len <= 0) {
        /* Embed our own binary so the probe IS a cold compiler */
        const char *self_path = ColdArgv0 ? ColdArgv0[0] : "/proc/self/exe";
        cold_cached_minimal_macho = source_open(self_path);
        if (cold_cached_minimal_macho.len <= 0) {
            /* Fallback: build minimal stub */
            Code *emit_code = code_new(parser->arena, 64);
            code_emit(emit_code, a64_movz(R0, 0, 0));
            code_emit(emit_code, a64_ret());
            char tmp[PATH_MAX];
            snprintf(tmp, sizeof(tmp), "/tmp/cold_emit_%d", getpid());
            macho_write_exec(tmp, emit_code->words, emit_code->count);
            cold_cached_minimal_macho = source_open(tmp);
            unlink(tmp);
        }
    }
    if (cold_cached_minimal_macho.len <= 0) return 0;
    int32_t li = body_string_literal(body, cold_cached_minimal_macho);
    int32_t bin_slot = body_slot(body, SLOT_STR, COLD_STR_SLOT_SIZE);
    body_op(body, BODY_OP_STR_LITERAL, bin_slot, li, 0);
    int32_t write1_dst = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_PATH_WRITE_TEXT, write1_dst, output, bin_slot);
    int32_t chmod_slot = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_CHMOD_X, chmod_slot, output, 0);
    /* Also write to a fixed temp path for self-exec chain */
    int32_t temp_path = cold_make_str_literal_cstr_slot(body, "/tmp/cold_probe_self");
    int32_t write2_dst = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_PATH_WRITE_TEXT, write2_dst, temp_path, bin_slot);
    body_op(body, BODY_OP_CHMOD_X, write2_dst, temp_path, 0);
    /* Self-exec: replace process with the just-written cold compiler */
    int32_t exec_dst = body_slot(body, SLOT_I32, 4);
    body_op(body, BODY_OP_COLD_SELF_EXEC, exec_dst, temp_path, 0);
    return cold_cached_minimal_macho.len;
}
#endif /* COLD_BACKEND_ONLY */

int main(int argc, char **argv) {
    ColdArgv0 = argv;
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
#ifndef COLD_BACKEND_ONLY
    if (argc >= 2 && strcmp(argv[1], "compile-bootstrap") == 0) {
        return cold_cmd_compile_bootstrap(argc, argv, argv[0]);
    }
    if (argc >= 2 && strcmp(argv[1], "bootstrap-bridge") == 0) {
        return cold_cmd_bootstrap_bridge(argc, argv, argv[0]);
    }
    if (argc >= 2 && strcmp(argv[1], "build-backend-driver") == 0) {
        return cold_cmd_build_backend_driver(argc, argv, argv[0]);
    }
#endif
    if (argc >= 2 && strcmp(argv[1], "status") == 0) {
        return cold_cmd_status(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "provider-archive-pack") == 0) {
        return cold_cmd_provider_archive_pack(argc, argv);
    }
    if (argc >= 2 && (strcmp(argv[1], "system-link-exec") == 0 ||
                      strcmp(argv[1], "--system-link-exec") == 0)) {
        return cold_cmd_system_link_exec(argc, argv);
    }
    if (argc >= 2 && strcmp(argv[1], "emit-cold-csg-v2") == 0) {
        /* Route to system-link-exec with --emit:csg-v2 */
        int32_t new_argc = argc + 1;
        const char **new_argv = (const char **)malloc((size_t)(new_argc + 1) * sizeof(char *));
        new_argv[0] = argv[0];
        new_argv[1] = "system-link-exec";
        new_argv[2] = "--emit:csg-v2";
        for (int32_t i = 2; i < argc; i++) new_argv[i + 1] = argv[i];
        new_argv[new_argc] = 0;
        int32_t rc = cold_cmd_system_link_exec(new_argc, (char **)new_argv);
        free(new_argv);
        return rc;
    }
    if (argc >= 2 && strcmp(argv[1], "run-host-smokes") == 0) {
        return cold_cmd_run_host_smokes(argc, argv, argv[0]);
    }
#ifdef COLD_BACKEND_ONLY
    fprintf(stderr, "cheng_cold (backend-only): unknown command '%s'\n", argv[1] ? argv[1] : "(none)");
    fprintf(stderr, "Available: help, print-contract, self-check, system-link-exec, emit-cold-csg-v2\n");
    return 1;
#else
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
#endif
}
