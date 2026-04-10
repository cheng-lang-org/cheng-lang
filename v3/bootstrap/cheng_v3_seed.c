#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef CHENG_V3_IMPL_SOURCE_PATH
#define CHENG_V3_IMPL_SOURCE_PATH __FILE__
#endif

#ifndef CHENG_V3_EMBEDDED_SOURCE_PATH
#define CHENG_V3_EMBEDDED_SOURCE_PATH ""
#endif

#ifndef CHENG_V3_EMBEDDED_CONTRACT_TEXT
#define CHENG_V3_EMBEDDED_CONTRACT_TEXT ""
#endif

#define CHENG_V3_MAX_FIELDS 32
#define CHENG_V3_MAX_CALL_ARGS 12
#define CHENG_V3_CALL_ARG_REGS 8
#define CHENG_V3_CALL_ARG_SPILL_STRIDE ((CHENG_V3_MAX_CALL_ARGS + 1) * 8)
#define CHENG_V3_MAX_UNSUPPORTED_DETAILS 64
#define CHENG_V3_MAX_TYPE_FIELDS 64
#define CHENG_V3_MAX_TYPE_DEFS 512

typedef struct {
    char *key;
    char *value;
} V3BootstrapField;

typedef struct {
    V3BootstrapField fields[CHENG_V3_MAX_FIELDS];
    size_t len;
    char source_path[PATH_MAX];
} V3BootstrapContract;

static const char *V3_REQUIRED_KEYS[] = {
    "syntax",
    "bootstrap_name",
    "bootstrap_entry",
    "compiler_class",
    "bootstrap_source_kind",
    "target",
    "compiler_parallel_model",
    "program_parallel_model",
    "ir_facts",
    "data_layout",
    "aot",
    "forbidden",
    "supported_commands",
    "compiler_entry_source",
    "compiler_runtime_source",
    "compiler_request_source",
    "lang_parser_source",
    "backend_system_link_plan_source",
    "backend_lowering_plan_source",
    "backend_primary_object_plan_source",
    "backend_object_plan_source",
    "backend_native_link_plan_source",
    "backend_system_link_exec_source",
    "runtime_core_runtime_source",
    "runtime_compiler_runtime_source",
    "tooling_bootstrap_contract_source",
    "backend_build_plan_source",
    "tooling_build_driver_script",
    "ordinary_command",
    "ordinary_pipeline_state",
    "user_program_auto_parallel",
    "bagua_bpi"
};

static void v3_usage(void) {
    puts("cheng_v3_seed");
    puts("usage:");
    puts("  cheng_v3_seed print-contract [--in:<path>]");
    puts("  cheng_v3_seed self-check [--in:<path>]");
    puts("  cheng_v3_seed status [--contract-in:<path>]");
    puts("  cheng_v3_seed print-build-plan [--contract-in:<path>]");
    puts("  cheng_v3_seed system-link-exec [--contract-in:<path>] [--in:<path>] [--root:<path>] --emit:exe --target:<triple> --out:<path> [--report-out:<path>]");
    puts("  cheng_v3_seed compile-bootstrap --in:<path> --out:<path> [--report-out:<path>]");
}

static void *v3_xmalloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "[cheng_v3_seed] out of memory size=%zu\n", size);
        exit(1);
    }
    return ptr;
}

static char *v3_strdup(const char *text) {
    size_t len = strlen(text);
    char *out = (char *)v3_xmalloc(len + 1);
    memcpy(out, text, len + 1);
    return out;
}

static char *v3_trim_inplace(char *text) {
    char *start = text;
    char *end;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return start;
}

static bool v3_streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

static bool v3_has_csv_token(const char *csv, const char *needle) {
    size_t needle_len = strlen(needle);
    const char *cursor = csv;
    while (*cursor != '\0') {
        while (*cursor == ' ' || *cursor == '\t' || *cursor == ',') {
            cursor++;
        }
        if (*cursor == '\0') {
            return false;
        }
        const char *stop = cursor;
        while (*stop != '\0' && *stop != ',') {
            stop++;
        }
        while (stop > cursor && (stop[-1] == ' ' || stop[-1] == '\t')) {
            stop--;
        }
        if ((size_t)(stop - cursor) == needle_len && strncmp(cursor, needle, needle_len) == 0) {
            return true;
        }
        cursor = stop;
        if (*cursor == ',') {
            cursor++;
        }
    }
    return false;
}

static uint64_t v3_fnv1a64(const char *text) {
    uint64_t hash = 1469598103934665603ULL;
    const unsigned char *cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
        hash ^= (uint64_t)(*cursor);
        hash *= 1099511628211ULL;
        cursor++;
    }
    return hash;
}

static char *v3_read_file(const char *path) {
    FILE *fp = fopen(path, "rb");
    long size;
    char *buffer;
    if (!fp) {
        fprintf(stderr, "[cheng_v3_seed] failed to open: %s\n", path);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        fprintf(stderr, "[cheng_v3_seed] failed to seek: %s\n", path);
        return NULL;
    }
    size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        fprintf(stderr, "[cheng_v3_seed] failed to size: %s\n", path);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        fprintf(stderr, "[cheng_v3_seed] failed to rewind: %s\n", path);
        return NULL;
    }
    buffer = (char *)v3_xmalloc((size_t)size + 1U);
    if (size > 0 && fread(buffer, 1U, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        free(buffer);
        fprintf(stderr, "[cheng_v3_seed] failed to read: %s\n", path);
        return NULL;
    }
    buffer[size] = '\0';
    fclose(fp);
    return buffer;
}

static void v3_contract_init(V3BootstrapContract *contract, const char *source_path) {
    size_t i;
    memset(contract, 0, sizeof(*contract));
    if (source_path && *source_path != '\0') {
        snprintf(contract->source_path, sizeof(contract->source_path), "%s", source_path);
    }
    for (i = 0; i < CHENG_V3_MAX_FIELDS; ++i) {
        contract->fields[i].key = NULL;
        contract->fields[i].value = NULL;
    }
}

static void v3_contract_free(V3BootstrapContract *contract) {
    size_t i;
    for (i = 0; i < contract->len; ++i) {
        free(contract->fields[i].key);
        free(contract->fields[i].value);
    }
    contract->len = 0;
}

static const char *v3_contract_get(const V3BootstrapContract *contract, const char *key) {
    size_t i;
    for (i = 0; i < contract->len; ++i) {
        if (v3_streq(contract->fields[i].key, key)) {
            return contract->fields[i].value;
        }
    }
    return NULL;
}

static bool v3_contract_add(V3BootstrapContract *contract, const char *key, const char *value) {
    size_t i;
    if (contract->len >= CHENG_V3_MAX_FIELDS) {
        fprintf(stderr, "[cheng_v3_seed] too many bootstrap fields\n");
        return false;
    }
    for (i = 0; i < contract->len; ++i) {
        if (v3_streq(contract->fields[i].key, key)) {
            fprintf(stderr, "[cheng_v3_seed] duplicate bootstrap field: %s\n", key);
            return false;
        }
    }
    contract->fields[contract->len].key = v3_strdup(key);
    contract->fields[contract->len].value = v3_strdup(value);
    contract->len += 1U;
    return true;
}

static bool v3_parse_contract_text(V3BootstrapContract *contract, const char *source_path, const char *text) {
    char *owned = v3_strdup(text);
    char *cursor = owned;
    char *line;
    v3_contract_init(contract, source_path);
    while ((line = strsep(&cursor, "\n")) != NULL) {
        char *comment = strchr(line, '#');
        char *eq;
        char *key;
        char *value;
        if (comment) {
            *comment = '\0';
        }
        line = v3_trim_inplace(line);
        if (*line == '\0') {
            continue;
        }
        eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "[cheng_v3_seed] invalid bootstrap line (missing '='): %s\n", line);
            free(owned);
            return false;
        }
        *eq = '\0';
        key = v3_trim_inplace(line);
        value = v3_trim_inplace(eq + 1);
        if (*key == '\0' || *value == '\0') {
            fprintf(stderr, "[cheng_v3_seed] invalid bootstrap line: %s = %s\n", key, value);
            free(owned);
            return false;
        }
        if (!v3_contract_add(contract, key, value)) {
            free(owned);
            return false;
        }
    }
    free(owned);
    return true;
}

static bool v3_parse_contract_file(V3BootstrapContract *contract, const char *path) {
    char *text = v3_read_file(path);
    bool ok;
    if (!text) {
        return false;
    }
    ok = v3_parse_contract_text(contract, path, text);
    free(text);
    return ok;
}

static bool v3_validate_contract(const V3BootstrapContract *contract) {
    static const char *REQUIRED_IR_FACTS[] = {
        "move", "borrow", "noalias", "escape", "layout", "abi"
    };
    static const char *REQUIRED_LAYOUT[] = {
        "dod", "soa", "fixed_layout"
    };
    static const char *REQUIRED_FORBIDDEN[] = {
        "local_payload",
        "exec_plan_payload",
        "payloadText",
        "BigInt",
        "legacy_proof_surface",
        "legacy_sidecar_mode"
    };
    size_t i;
    const char *ir_facts;
    const char *data_layout;
    const char *forbidden;
    const char *commands;
    for (i = 0; i < sizeof(V3_REQUIRED_KEYS) / sizeof(V3_REQUIRED_KEYS[0]); ++i) {
        if (!v3_contract_get(contract, V3_REQUIRED_KEYS[i])) {
            fprintf(stderr, "[cheng_v3_seed] missing bootstrap field: %s\n", V3_REQUIRED_KEYS[i]);
            return false;
        }
    }
    if (!v3_streq(v3_contract_get(contract, "syntax"), "v3-bootstrap-v1")) {
        fprintf(stderr, "[cheng_v3_seed] unsupported bootstrap syntax\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "compiler_class"), "bootstrap_subset")) {
        fprintf(stderr, "[cheng_v3_seed] bootstrap compiler_class must be bootstrap_subset\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "target"), "arm64-apple-darwin")) {
        fprintf(stderr, "[cheng_v3_seed] bootstrap target must be arm64-apple-darwin\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "compiler_parallel_model"),
                  "two_pass,function_scheduler,thread_local_arena")) {
        fprintf(stderr, "[cheng_v3_seed] compiler parallel model drift\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "program_parallel_model"),
                  "deterministic_single_thread")) {
        fprintf(stderr, "[cheng_v3_seed] program parallel model drift\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "aot"), "required")) {
        fprintf(stderr, "[cheng_v3_seed] aot must be required\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "user_program_auto_parallel"), "off")) {
        fprintf(stderr, "[cheng_v3_seed] user_program_auto_parallel must be off\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "bagua_bpi"), "sidecar_only")) {
        fprintf(stderr, "[cheng_v3_seed] bagua_bpi must be sidecar_only\n");
        return false;
    }
    ir_facts = v3_contract_get(contract, "ir_facts");
    data_layout = v3_contract_get(contract, "data_layout");
    forbidden = v3_contract_get(contract, "forbidden");
    commands = v3_contract_get(contract, "supported_commands");
    for (i = 0; i < sizeof(REQUIRED_IR_FACTS) / sizeof(REQUIRED_IR_FACTS[0]); ++i) {
        if (!v3_has_csv_token(ir_facts, REQUIRED_IR_FACTS[i])) {
            fprintf(stderr, "[cheng_v3_seed] missing ir fact: %s\n", REQUIRED_IR_FACTS[i]);
            return false;
        }
    }
    for (i = 0; i < sizeof(REQUIRED_LAYOUT) / sizeof(REQUIRED_LAYOUT[0]); ++i) {
        if (!v3_has_csv_token(data_layout, REQUIRED_LAYOUT[i])) {
            fprintf(stderr, "[cheng_v3_seed] missing data layout default: %s\n", REQUIRED_LAYOUT[i]);
            return false;
        }
    }
    for (i = 0; i < sizeof(REQUIRED_FORBIDDEN) / sizeof(REQUIRED_FORBIDDEN[0]); ++i) {
        if (!v3_has_csv_token(forbidden, REQUIRED_FORBIDDEN[i])) {
            fprintf(stderr, "[cheng_v3_seed] missing forbidden token: %s\n", REQUIRED_FORBIDDEN[i]);
            return false;
        }
    }
    if (!v3_has_csv_token(commands, "print-contract") ||
        !v3_has_csv_token(commands, "self-check") ||
        !v3_has_csv_token(commands, "compile-bootstrap") ||
        !v3_has_csv_token(commands, "status") ||
        !v3_has_csv_token(commands, "print-build-plan") ||
        !v3_has_csv_token(commands, "system-link-exec")) {
        fprintf(stderr, "[cheng_v3_seed] supported_commands drift\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "ordinary_command"), "system-link-exec")) {
        fprintf(stderr, "[cheng_v3_seed] ordinary_command drift\n");
        return false;
    }
    if (!v3_streq(v3_contract_get(contract, "ordinary_pipeline_state"), "primary_object_codegen_missing")) {
        fprintf(stderr, "[cheng_v3_seed] ordinary_pipeline_state drift\n");
        return false;
    }
    return true;
}

static char *v3_contract_normalized_text(const V3BootstrapContract *contract) {
    static const char *ORDER[] = {
        "syntax",
        "bootstrap_name",
        "bootstrap_entry",
        "compiler_class",
        "bootstrap_source_kind",
        "target",
        "compiler_parallel_model",
        "program_parallel_model",
        "ir_facts",
        "data_layout",
        "aot",
        "forbidden",
        "supported_commands",
        "compiler_entry_source",
        "compiler_runtime_source",
        "compiler_request_source",
        "lang_parser_source",
        "backend_system_link_plan_source",
        "backend_lowering_plan_source",
        "backend_primary_object_plan_source",
        "backend_object_plan_source",
        "backend_native_link_plan_source",
        "backend_system_link_exec_source",
        "runtime_core_runtime_source",
        "runtime_compiler_runtime_source",
        "tooling_bootstrap_contract_source",
        "backend_build_plan_source",
        "tooling_build_driver_script",
        "ordinary_command",
        "ordinary_pipeline_state",
        "user_program_auto_parallel",
        "bagua_bpi"
    };
    size_t i;
    size_t cap = 8192U;
    size_t used = 0U;
    char *out = (char *)v3_xmalloc(cap);
    out[0] = '\0';
    for (i = 0; i < sizeof(ORDER) / sizeof(ORDER[0]); ++i) {
        const char *value = v3_contract_get(contract, ORDER[i]);
        int wrote;
        if (!value) {
            continue;
        }
        wrote = snprintf(out + used, cap - used, "%s=%s\n", ORDER[i], value);
        if (wrote < 0) {
            free(out);
            return NULL;
        }
        if (used + (size_t)wrote + 1U >= cap) {
            cap *= 2U;
            out = (char *)realloc(out, cap);
            if (!out) {
                fprintf(stderr, "[cheng_v3_seed] out of memory while normalizing contract\n");
                exit(1);
            }
            wrote = snprintf(out + used, cap - used, "%s=%s\n", ORDER[i], value);
            if (wrote < 0) {
                free(out);
                return NULL;
            }
        }
        used += (size_t)wrote;
    }
    return out;
}

static char *v3_c_escape(const char *text) {
    size_t i;
    size_t len = 0U;
    char *out;
    char *cursor;
    for (i = 0; text[i] != '\0'; ++i) {
        switch (text[i]) {
            case '\\':
            case '"':
                len += 2U;
                break;
            case '\n':
            case '\r':
            case '\t':
                len += 2U;
                break;
            default:
                len += 1U;
                break;
        }
    }
    out = (char *)v3_xmalloc(len + 1U);
    cursor = out;
    for (i = 0; text[i] != '\0'; ++i) {
        switch (text[i]) {
            case '\\':
                *cursor++ = '\\';
                *cursor++ = '\\';
                break;
            case '"':
                *cursor++ = '\\';
                *cursor++ = '"';
                break;
            case '\n':
                *cursor++ = '\\';
                *cursor++ = 'n';
                break;
            case '\r':
                *cursor++ = '\\';
                *cursor++ = 'r';
                break;
            case '\t':
                *cursor++ = '\\';
                *cursor++ = 't';
                break;
            default:
                *cursor++ = text[i];
                break;
        }
    }
    *cursor = '\0';
    return out;
}

static bool v3_write_text_file(const char *path, const char *text) {
    FILE *fp = fopen(path, "wb");
    size_t len = strlen(text);
    if (!fp) {
        fprintf(stderr, "[cheng_v3_seed] failed to write: %s\n", path);
        return false;
    }
    if (len > 0 && fwrite(text, 1U, len, fp) != len) {
        fclose(fp);
        fprintf(stderr, "[cheng_v3_seed] failed to flush: %s\n", path);
        return false;
    }
    fclose(fp);
    return true;
}

static bool v3_mkdir_p(const char *path) {
    char temp[PATH_MAX];
    size_t i;
    if (!path || *path == '\0') {
        return true;
    }
    if (strlen(path) >= sizeof(temp)) {
        fprintf(stderr, "[cheng_v3_seed] path too long: %s\n", path);
        return false;
    }
    snprintf(temp, sizeof(temp), "%s", path);
    for (i = 1U; temp[i] != '\0'; ++i) {
        if (temp[i] == '/') {
            temp[i] = '\0';
            if (temp[0] != '\0' && mkdir(temp, 0777) != 0 && errno != EEXIST) {
                fprintf(stderr, "[cheng_v3_seed] mkdir failed: %s\n", temp);
                return false;
            }
            temp[i] = '/';
        }
    }
    if (mkdir(temp, 0777) != 0 && errno != EEXIST) {
        fprintf(stderr, "[cheng_v3_seed] mkdir failed: %s\n", temp);
        return false;
    }
    return true;
}

static bool v3_ensure_parent_dir(const char *path) {
    char temp[PATH_MAX];
    char *slash;
    if (strlen(path) >= sizeof(temp)) {
        fprintf(stderr, "[cheng_v3_seed] path too long: %s\n", path);
        return false;
    }
    snprintf(temp, sizeof(temp), "%s", path);
    slash = strrchr(temp, '/');
    if (!slash) {
        return true;
    }
    if (slash == temp) {
        return true;
    }
    *slash = '\0';
    return v3_mkdir_p(temp);
}

static char *v3_shell_quote(const char *text) {
    size_t i;
    size_t len = 2U;
    char *out;
    char *cursor;
    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\'') {
            len += 4U;
        } else {
            len += 1U;
        }
    }
    out = (char *)v3_xmalloc(len + 1U);
    cursor = out;
    *cursor++ = '\'';
    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '\'') {
            *cursor++ = '\'';
            *cursor++ = '\\';
            *cursor++ = '\'';
            *cursor++ = '\'';
        } else {
            *cursor++ = text[i];
        }
    }
    *cursor++ = '\'';
    *cursor = '\0';
    return out;
}

static bool v3_compile_wrapper(const char *wrapper_path, const char *out_path) {
    const char *cc = getenv("CC");
    char command[PATH_MAX * 3];
    char *quoted_cc;
    char *quoted_wrapper;
    char *quoted_out;
    int rc;
    if (!cc || *cc == '\0') {
        cc = "cc";
    }
    quoted_cc = v3_shell_quote(cc);
    quoted_wrapper = v3_shell_quote(wrapper_path);
    quoted_out = v3_shell_quote(out_path);
    snprintf(command, sizeof(command),
             "%s -std=c11 -O2 -Wall -Wextra -pedantic %s -o %s",
             quoted_cc, quoted_wrapper, quoted_out);
    free(quoted_cc);
    free(quoted_wrapper);
    free(quoted_out);
    rc = system(command);
    if (rc != 0) {
        fprintf(stderr, "[cheng_v3_seed] cc failed rc=%d command=%s\n", rc, command);
        return false;
    }
    return true;
}

static bool v3_write_wrapper_source(const char *wrapper_path,
                                    const char *embedded_source_path,
                                    const char *contract_text) {
    char *escaped_impl = v3_c_escape(CHENG_V3_IMPL_SOURCE_PATH);
    char *escaped_source = v3_c_escape(embedded_source_path);
    char *escaped_contract = v3_c_escape(contract_text);
    size_t cap = strlen(escaped_impl) + strlen(escaped_source) + strlen(escaped_contract) + 256U;
    char *text = (char *)v3_xmalloc(cap);
    snprintf(text, cap,
             "#define CHENG_V3_IMPL_SOURCE_PATH \"%s\"\n"
             "#define CHENG_V3_EMBEDDED_SOURCE_PATH \"%s\"\n"
             "#define CHENG_V3_EMBEDDED_CONTRACT_TEXT \"%s\"\n"
             "#include CHENG_V3_IMPL_SOURCE_PATH\n",
             escaped_impl, escaped_source, escaped_contract);
    free(escaped_impl);
    free(escaped_source);
    free(escaped_contract);
    if (!v3_ensure_parent_dir(wrapper_path)) {
        free(text);
        return false;
    }
    if (!v3_write_text_file(wrapper_path, text)) {
        free(text);
        return false;
    }
    free(text);
    return true;
}

static bool v3_write_compile_report(const char *report_path,
                                    const V3BootstrapContract *contract,
                                    const char *source_path,
                                    const char *out_path,
                                    const char *contract_text) {
    char buffer[8192];
    snprintf(buffer, sizeof(buffer),
             "compiler_class=%s\n"
             "target=%s\n"
             "source=%s\n"
             "output=%s\n"
             "contract_hash=%016llx\n"
             "compiler_parallel_model=%s\n"
             "program_parallel_model=%s\n"
             "data_layout=%s\n",
             v3_contract_get(contract, "compiler_class"),
             v3_contract_get(contract, "target"),
             source_path,
             out_path,
             (unsigned long long)v3_fnv1a64(contract_text),
             v3_contract_get(contract, "compiler_parallel_model"),
             v3_contract_get(contract, "program_parallel_model"),
             v3_contract_get(contract, "data_layout"));
    if (!v3_ensure_parent_dir(report_path)) {
        return false;
    }
    return v3_write_text_file(report_path, buffer);
}

static const char *v3_flag_value(int argc, char **argv, const char *flag) {
    size_t flag_len = strlen(flag);
    int i;
    for (i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (strncmp(arg, flag, flag_len) == 0) {
            if (arg[flag_len] == ':') {
                return arg + flag_len + 1;
            }
            if (arg[flag_len] == '=') {
                return arg + flag_len + 1;
            }
            if (arg[flag_len] == '\0' && (i + 1) < argc) {
                return argv[i + 1];
            }
        }
    }
    return NULL;
}

#define CHENG_V3_MAX_PLAN_PATHS 256
#define CHENG_V3_MAX_PLAN_REASONS 16
#define CHENG_V3_MAX_RUNTIME_TARGETS 8
#define CHENG_V3_MAX_PROVIDER_MODULES 8
#define CHENG_V3_MAX_PLAN_FUNCTIONS 4096
#define CHENG_V3_MAX_IMPORT_ALIASES 128
#define CHENG_V3_MAX_CALLEES 64
#define CHENG_V3_CONST_MAX_FIELDS 64
#define CHENG_V3_CONST_MAX_BINDINGS 64
#define CHENG_V3_CONST_MAX_ECHOS 8
#define CHENG_V3_MAX_TOP_LEVEL_CONSTS 2048
#define CHENG_V3_MAX_ASM_LOCALS 256

typedef struct {
    char text[PATH_MAX];
} V3PlanPath;

typedef struct {
    char owner_module_path[PATH_MAX];
    char target_module_path[PATH_MAX];
    char target_source_path[PATH_MAX];
    bool resolved;
} V3PlanImportEdge;

typedef struct {
    char alias_text[128];
    char module_path[PATH_MAX];
} V3ImportAlias;

typedef struct {
    char workspace_root[PATH_MAX];
    char package_root[PATH_MAX];
    char package_id[PATH_MAX];
    char entry_path[PATH_MAX];
    char output_path[PATH_MAX];
    char target_triple[128];
    char module_stem[PATH_MAX];
    char module_kind[64];
    char emit_kind[32];
    char entry_symbol[64];
    size_t runtime_target_count;
    V3PlanPath runtime_targets[CHENG_V3_MAX_RUNTIME_TARGETS];
    size_t provider_module_count;
    V3PlanPath provider_modules[CHENG_V3_MAX_PROVIDER_MODULES];
    size_t source_closure_count;
    V3PlanPath source_closure_paths[CHENG_V3_MAX_PLAN_PATHS];
    size_t owner_module_count;
    V3PlanPath owner_modules[8];
    size_t import_edge_count;
    size_t unresolved_import_count;
    bool has_compiler_top_level;
    bool main_function_present;
    size_t missing_reason_count;
    char missing_reasons[CHENG_V3_MAX_PLAN_REASONS][128];
} V3SystemLinkPlanStub;

typedef struct {
    char source_path[PATH_MAX];
    char owner_module_path[PATH_MAX];
    char function_name[128];
    char symbol_text[PATH_MAX];
    size_t signature_line_number;
    size_t body_first_line_number;
    size_t body_last_line_number;
    size_t param_count;
    char param_names[32][128];
    char param_types[32][128];
    bool param_is_var[32];
    char param_abi_classes[32][32];
    char return_type[128];
    char return_abi_class[32];
    char body_kind[64];
    char body_call_target[128];
    char first_body_line[512];
    size_t callee_count;
    V3PlanPath callee_symbols[CHENG_V3_MAX_CALLEES];
    bool is_entry;
    bool reachable;
} V3LoweredFunctionStub;

typedef struct {
    char symbol_text[PATH_MAX];
    char owner_module_path[PATH_MAX];
    char const_name[128];
    char type_text[128];
    char abi_class[32];
    int64_t i64_value;
    bool bool_value;
} V3TopLevelConstStub;

typedef struct {
    char name[128];
    char type_text[128];
    char abi_class[32];
    int32_t stack_offset;
    int32_t stack_size;
    int32_t stack_align;
    bool indirect_value;
} V3AsmLocalSlot;

typedef enum {
    V3_TYPE_DEF_NONE = 0,
    V3_TYPE_DEF_ALIAS,
    V3_TYPE_DEF_RECORD
} V3TypeDefKind;

typedef struct {
    char name[128];
    char type_text[256];
} V3TypeFieldDef;

typedef struct {
    char owner_module_path[PATH_MAX];
    char type_name[128];
    V3TypeDefKind kind;
    char alias_target[256];
    size_t field_count;
    V3TypeFieldDef fields[CHENG_V3_MAX_TYPE_FIELDS];
} V3TypeDefStub;

typedef struct {
    int32_t size;
    int32_t align;
} V3TypeLayoutStub;

typedef struct {
    size_t function_count;
    V3LoweredFunctionStub functions[CHENG_V3_MAX_PLAN_FUNCTIONS];
    size_t const_count;
    V3TopLevelConstStub consts[CHENG_V3_MAX_TOP_LEVEL_CONSTS];
    size_t type_def_count;
    V3TypeDefStub type_defs[CHENG_V3_MAX_TYPE_DEFS];
    size_t missing_reason_count;
    char missing_reasons[CHENG_V3_MAX_PLAN_REASONS][128];
} V3LoweringPlanStub;

typedef struct {
    char object_format[32];
    char output_path[PATH_MAX];
    char entry_path[PATH_MAX];
    char entry_symbol[128];
    bool consteval_entry_ready;
    size_t consteval_echo_count;
    V3PlanPath consteval_echo_texts[CHENG_V3_CONST_MAX_ECHOS];
    int32_t consteval_return_code;
    size_t item_count;
    int32_t item_ids[CHENG_V3_MAX_PLAN_FUNCTIONS];
    V3PlanPath symbol_names[CHENG_V3_MAX_PLAN_FUNCTIONS];
    size_t instruction_word_count;
    size_t data_label_count;
    size_t reloc_count;
    size_t unsupported_function_count;
    V3PlanPath unsupported_function_symbols[CHENG_V3_MAX_UNSUPPORTED_DETAILS];
    char unsupported_body_kinds[CHENG_V3_MAX_UNSUPPORTED_DETAILS][128];
    char unsupported_first_lines[CHENG_V3_MAX_UNSUPPORTED_DETAILS][128];
    char unsupported_abi_summaries[CHENG_V3_MAX_UNSUPPORTED_DETAILS][256];
    size_t missing_reason_count;
    char missing_reasons[CHENG_V3_MAX_PLAN_REASONS][128];
} V3PrimaryObjectPlanStub;

typedef struct {
    char primary_object_path[PATH_MAX];
    size_t provider_source_count;
    V3PlanPath provider_source_paths[CHENG_V3_MAX_PROVIDER_MODULES];
    size_t provider_object_count;
    V3PlanPath provider_object_paths[CHENG_V3_MAX_PROVIDER_MODULES];
    size_t link_input_count;
    V3PlanPath link_input_paths[CHENG_V3_MAX_PLAN_PATHS];
    size_t missing_reason_count;
    char missing_reasons[CHENG_V3_MAX_PLAN_REASONS][128];
} V3ObjectPlanStub;

typedef struct {
    char linker_program[PATH_MAX];
    char linker_flavor[64];
    char output_kind[64];
    char primary_object_path[PATH_MAX];
    char entry_symbol[128];
    size_t link_input_count;
    V3PlanPath link_input_paths[CHENG_V3_MAX_PLAN_PATHS];
    size_t missing_reason_count;
    char missing_reasons[CHENG_V3_MAX_PLAN_REASONS][128];
} V3NativeLinkPlanStub;

typedef enum {
    V3_CONST_INVALID = 0,
    V3_CONST_BOOL,
    V3_CONST_I32,
    V3_CONST_STR,
    V3_CONST_SYMBOL,
    V3_CONST_RECORD
} V3ConstKind;

typedef struct V3ConstRecord V3ConstRecord;

typedef struct {
    V3ConstKind kind;
    bool bool_value;
    int32_t i32_value;
    char *text_value;
    V3ConstRecord *record_value;
} V3ConstValue;

struct V3ConstRecord {
    size_t field_count;
    char field_names[CHENG_V3_CONST_MAX_FIELDS][128];
    V3ConstValue field_values[CHENG_V3_CONST_MAX_FIELDS];
};

typedef struct {
    char name[128];
    V3ConstValue value;
} V3ConstBinding;

typedef struct {
    size_t binding_count;
    V3ConstBinding bindings[CHENG_V3_CONST_MAX_BINDINGS];
} V3ConstEnv;

typedef struct {
    bool ready;
    size_t echo_count;
    V3PlanPath echo_texts[CHENG_V3_CONST_MAX_ECHOS];
    int32_t return_code;
} V3ConstEntryProgram;

typedef struct {
    bool external;
    const V3LoweredFunctionStub *lowered_function;
    char callee_name[PATH_MAX];
    char symbol_name[PATH_MAX];
    char return_type[128];
    char return_abi_class[32];
    size_t param_count;
    char param_types[CHENG_V3_MAX_CALL_ARGS][128];
    bool param_is_var[CHENG_V3_MAX_CALL_ARGS];
    char param_abi_classes[CHENG_V3_MAX_CALL_ARGS][32];
} V3AsmCallTarget;

typedef enum {
    V3_RESULT_INTRINSIC_NONE = 0,
    V3_RESULT_INTRINSIC_IS_OK,
    V3_RESULT_INTRINSIC_IS_ERR,
    V3_RESULT_INTRINSIC_VALUE,
    V3_RESULT_INTRINSIC_ERROR,
    V3_RESULT_INTRINSIC_ERROR_TEXT,
    V3_RESULT_INTRINSIC_ERROR_INFO_OF,
    V3_RESULT_INTRINSIC_OK,
    V3_RESULT_INTRINSIC_ERR,
    V3_RESULT_INTRINSIC_ERR_CODE,
    V3_RESULT_INTRINSIC_ERR_INFO
} V3ResultIntrinsicKind;

static bool v3_startswith(const char *text, const char *prefix) {
    size_t n = strlen(prefix);
    return strncmp(text, prefix, n) == 0;
}

static void v3_copy_text(char *dst, size_t cap, const char *src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static void v3_const_value_init(V3ConstValue *value) {
    memset(value, 0, sizeof(*value));
    value->kind = V3_CONST_INVALID;
}

static void v3_const_value_free(V3ConstValue *value);

static void v3_const_record_free(V3ConstRecord *record) {
    size_t i;
    if (!record) {
        return;
    }
    for (i = 0; i < record->field_count; ++i) {
        v3_const_value_free(&record->field_values[i]);
    }
    free(record);
}

static void v3_const_value_free(V3ConstValue *value) {
    if (!value) {
        return;
    }
    if ((value->kind == V3_CONST_STR || value->kind == V3_CONST_SYMBOL) &&
        value->text_value != NULL) {
        free(value->text_value);
    }
    if (value->kind == V3_CONST_RECORD && value->record_value != NULL) {
        v3_const_record_free(value->record_value);
    }
    v3_const_value_init(value);
}

static bool v3_const_value_set_str_kind(V3ConstValue *value,
                                        V3ConstKind kind,
                                        const char *text) {
    v3_const_value_free(value);
    value->kind = kind;
    value->text_value = v3_strdup(text ? text : "");
    return true;
}

static bool v3_const_value_set_bool(V3ConstValue *value, bool bool_value) {
    v3_const_value_free(value);
    value->kind = V3_CONST_BOOL;
    value->bool_value = bool_value;
    return true;
}

static bool v3_const_value_set_i32(V3ConstValue *value, int32_t i32_value) {
    v3_const_value_free(value);
    value->kind = V3_CONST_I32;
    value->i32_value = i32_value;
    return true;
}

static bool v3_const_value_clone(const V3ConstValue *src, V3ConstValue *dst) {
    size_t i;
    v3_const_value_free(dst);
    dst->kind = src->kind;
    dst->bool_value = src->bool_value;
    dst->i32_value = src->i32_value;
    if ((src->kind == V3_CONST_STR || src->kind == V3_CONST_SYMBOL) &&
        src->text_value != NULL) {
        dst->text_value = v3_strdup(src->text_value);
        return true;
    }
    if (src->kind == V3_CONST_RECORD && src->record_value != NULL) {
        V3ConstRecord *record = (V3ConstRecord *)v3_xmalloc(sizeof(V3ConstRecord));
        memset(record, 0, sizeof(*record));
        record->field_count = src->record_value->field_count;
        for (i = 0; i < src->record_value->field_count; ++i) {
            snprintf(record->field_names[i], sizeof(record->field_names[i]), "%s",
                     src->record_value->field_names[i]);
            v3_const_value_init(&record->field_values[i]);
            if (!v3_const_value_clone(&src->record_value->field_values[i],
                                      &record->field_values[i])) {
                v3_const_record_free(record);
                return false;
            }
        }
        dst->record_value = record;
    }
    return true;
}

static const V3ConstValue *v3_const_record_field(const V3ConstValue *record_value,
                                                 const char *field_name) {
    size_t i;
    if (!record_value || record_value->kind != V3_CONST_RECORD || record_value->record_value == NULL) {
        return NULL;
    }
    for (i = 0; i < record_value->record_value->field_count; ++i) {
        if (strcmp(record_value->record_value->field_names[i], field_name) == 0) {
            return &record_value->record_value->field_values[i];
        }
    }
    return NULL;
}

static bool v3_const_env_set(V3ConstEnv *env, const char *name, const V3ConstValue *value) {
    size_t i;
    for (i = 0; i < env->binding_count; ++i) {
        if (strcmp(env->bindings[i].name, name) == 0) {
            return v3_const_value_clone(value, &env->bindings[i].value);
        }
    }
    if (env->binding_count >= CHENG_V3_CONST_MAX_BINDINGS) {
        return false;
    }
    snprintf(env->bindings[env->binding_count].name,
             sizeof(env->bindings[env->binding_count].name),
             "%s",
             name);
    v3_const_value_init(&env->bindings[env->binding_count].value);
    if (!v3_const_value_clone(value, &env->bindings[env->binding_count].value)) {
        return false;
    }
    env->binding_count += 1U;
    return true;
}

static const V3ConstValue *v3_const_env_get(const V3ConstEnv *env, const char *name) {
    size_t i;
    for (i = 0; i < env->binding_count; ++i) {
        if (strcmp(env->bindings[i].name, name) == 0) {
            return &env->bindings[i].value;
        }
    }
    return NULL;
}

static void v3_const_env_free(V3ConstEnv *env) {
    size_t i;
    for (i = 0; i < env->binding_count; ++i) {
        v3_const_value_free(&env->bindings[i].value);
    }
    env->binding_count = 0U;
}

static void v3_trim_copy_text(const char *src, char *dst, size_t cap) {
    char temp[8192];
    if (strlen(src) >= sizeof(temp)) {
        v3_copy_text(dst, cap, src);
        return;
    }
    snprintf(temp, sizeof(temp), "%s", src);
    v3_copy_text(dst, cap, v3_trim_inplace(temp));
}

static int32_t v3_find_top_level_binary_op(const char *text, const char *op) {
    bool in_string = false;
    int32_t depth = 0;
    size_t op_len = strlen(op);
    size_t i;
    for (i = 0; text[i] != '\0'; ++i) {
        if (text[i] == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (text[i] == '(') {
            depth += 1;
            continue;
        }
        if (text[i] == ')') {
            depth -= 1;
            continue;
        }
        if (depth == 0 && strncmp(text + i, op, op_len) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool v3_split_top_level_args(const char *text,
                                    char items[][4096],
                                    size_t *item_count,
                                    size_t item_cap) {
    bool in_string = false;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    int32_t brace_depth = 0;
    const char *start = text;
    size_t i;
    *item_count = 0U;
    for (i = 0; ; ++i) {
        const char ch = text[i];
        if (ch == '"') {
            in_string = !in_string;
        } else if (!in_string && ch == '(') {
            paren_depth += 1;
        } else if (!in_string && ch == ')') {
            paren_depth -= 1;
        } else if (!in_string && ch == '[') {
            bracket_depth += 1;
        } else if (!in_string && ch == ']') {
            bracket_depth -= 1;
        } else if (!in_string && ch == '{') {
            brace_depth += 1;
        } else if (!in_string && ch == '}') {
            brace_depth -= 1;
        }
        if ((!in_string &&
             paren_depth == 0 &&
             bracket_depth == 0 &&
             brace_depth == 0 &&
             ch == ',') || ch == '\0') {
            if (*item_count >= item_cap) {
                return false;
            }
            snprintf(items[*item_count], sizeof(items[*item_count]), "%.*s", (int)(text + i - start), start);
            v3_trim_copy_text(items[*item_count], items[*item_count], sizeof(items[*item_count]));
            *item_count += 1U;
            start = text + i + 1;
        }
        if (ch == '\0') {
            break;
        }
    }
    if (*item_count == 1U && items[0][0] == '\0') {
        *item_count = 0U;
    }
    return true;
}

static bool v3_parse_call_text(const char *text,
                               char *callee,
                               size_t callee_cap,
                               char args[][4096],
                               size_t *arg_count,
                               size_t arg_cap) {
    char trimmed[8192];
    const char *open_paren = NULL;
    const char *close_paren = NULL;
    bool in_string = false;
    int32_t bracket_depth = 0;
    int32_t paren_depth = 0;
    char inner[8192];
    size_t i;
    v3_trim_copy_text(text, trimmed, sizeof(trimmed));
    for (i = 0; trimmed[i] != '\0'; ++i) {
        char ch = trimmed[i];
        if (ch == '"' && (i == 0U || trimmed[i - 1U] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '[') {
            bracket_depth += 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth -= 1;
            continue;
        }
        if (ch == '(' && bracket_depth == 0) {
            if (!open_paren) {
                open_paren = trimmed + i;
            }
            paren_depth += 1;
            continue;
        }
        if (ch == ')' && bracket_depth == 0) {
            paren_depth -= 1;
            if (paren_depth == 0) {
                close_paren = trimmed + i;
            }
            continue;
        }
    }
    if (!open_paren || !close_paren || close_paren <= open_paren || paren_depth != 0 || bracket_depth != 0) {
        return false;
    }
    if (trimmed[strlen(trimmed) - 1U] != ')') {
        return false;
    }
    snprintf(callee, callee_cap, "%.*s", (int)(open_paren - trimmed), trimmed);
    v3_trim_copy_text(callee, callee, callee_cap);
    {
        int32_t callee_bracket_depth = 0;
        if (callee[0] == '\0') {
            return false;
        }
        for (i = 0; callee[i] != '\0'; ++i) {
            char ch = callee[i];
            if (ch == '[') {
                callee_bracket_depth += 1;
                continue;
            }
            if (ch == ']') {
                callee_bracket_depth -= 1;
                if (callee_bracket_depth < 0) {
                    return false;
                }
                continue;
            }
            if (callee_bracket_depth == 0 &&
                (isspace((unsigned char)ch) ||
                 ch == '&' || ch == '|' || ch == '+' || ch == '-' ||
                 ch == '*' || ch == '/' || ch == '%' || ch == '!' ||
                 ch == '<' || ch == '>' || ch == '=' || ch == '?')) {
                return false;
            }
        }
        if (callee_bracket_depth != 0) {
            return false;
        }
    }
    snprintf(inner, sizeof(inner), "%.*s", (int)(close_paren - open_paren - 1), open_paren + 1);
    return v3_split_top_level_args(inner, args, arg_count, arg_cap);
}

static bool v3_parse_list_literal_text(const char *text,
                                       char items[][4096],
                                       size_t *item_count,
                                       size_t item_cap) {
    char trimmed[8192];
    char inner[8192];
    size_t len;
    v3_trim_copy_text(text, trimmed, sizeof(trimmed));
    len = strlen(trimmed);
    if (len < 2U || trimmed[0] != '[' || trimmed[len - 1U] != ']') {
        return false;
    }
    snprintf(inner, sizeof(inner), "%.*s", (int)(len - 2U), trimmed + 1);
    return v3_split_top_level_args(inner, items, item_count, item_cap);
}

static bool v3_is_list_literal_text(const char *text, bool *nonempty_out) {
    char trimmed[8192];
    size_t len;
    v3_trim_copy_text(text, trimmed, sizeof(trimmed));
    len = strlen(trimmed);
    if (len < 2U || trimmed[0] != '[' || trimmed[len - 1U] != ']') {
        return false;
    }
    if (nonempty_out) {
        *nonempty_out = len > 2U;
    }
    return true;
}

static bool v3_parse_ternary_expr(const char *text,
                                  char *cond_out,
                                  size_t cond_cap,
                                  char *true_out,
                                  size_t true_cap,
                                  char *false_out,
                                  size_t false_cap) {
    char trimmed[8192];
    bool in_string = false;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    int32_t brace_depth = 0;
    int32_t q_index = -1;
    int32_t colon_index = -1;
    size_t i;
    v3_trim_copy_text(text, trimmed, sizeof(trimmed));
    for (i = 0; trimmed[i] != '\0'; ++i) {
        char ch = trimmed[i];
        if (ch == '"' && (i == 0 || trimmed[i - 1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            paren_depth += 1;
            continue;
        }
        if (ch == ')') {
            paren_depth -= 1;
            continue;
        }
        if (ch == '[') {
            bracket_depth += 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth -= 1;
            continue;
        }
        if (ch == '{') {
            brace_depth += 1;
            continue;
        }
        if (ch == '}') {
            brace_depth -= 1;
            continue;
        }
        if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0) {
            if (ch == '?' && q_index < 0) {
                q_index = (int32_t)i;
                continue;
            }
            if (ch == ':' && q_index >= 0) {
                colon_index = (int32_t)i;
                break;
            }
        }
    }
    if (q_index <= 0 || colon_index <= q_index + 1) {
        return false;
    }
    snprintf(cond_out, cond_cap, "%.*s", q_index, trimmed);
    snprintf(true_out, true_cap, "%.*s", colon_index - q_index - 1, trimmed + q_index + 1);
    snprintf(false_out, false_cap, "%s", trimmed + colon_index + 1);
    v3_trim_copy_text(cond_out, cond_out, cond_cap);
    v3_trim_copy_text(true_out, true_out, true_cap);
    v3_trim_copy_text(false_out, false_out, false_cap);
    return cond_out[0] != '\0' && true_out[0] != '\0' && false_out[0] != '\0';
}

static int32_t v3_next_expr_label_id(void) {
    static int32_t next_id = 0;
    next_id += 1;
    return next_id;
}

#define CHENG_V3_MAX_LOOP_LABEL_DEPTH 64

static char cheng_v3_loop_continue_labels[CHENG_V3_MAX_LOOP_LABEL_DEPTH][128];
static char cheng_v3_loop_break_labels[CHENG_V3_MAX_LOOP_LABEL_DEPTH][128];
static size_t cheng_v3_loop_label_depth = 0U;

static void v3_reset_loop_labels(void) {
    cheng_v3_loop_label_depth = 0U;
}

static bool v3_push_loop_labels(const char *continue_label, const char *break_label) {
    if (cheng_v3_loop_label_depth >= CHENG_V3_MAX_LOOP_LABEL_DEPTH) {
        return false;
    }
    snprintf(cheng_v3_loop_continue_labels[cheng_v3_loop_label_depth],
             sizeof(cheng_v3_loop_continue_labels[cheng_v3_loop_label_depth]),
             "%s",
             continue_label ? continue_label : "");
    snprintf(cheng_v3_loop_break_labels[cheng_v3_loop_label_depth],
             sizeof(cheng_v3_loop_break_labels[cheng_v3_loop_label_depth]),
             "%s",
             break_label ? break_label : "");
    cheng_v3_loop_label_depth += 1U;
    return true;
}

static void v3_pop_loop_labels(void) {
    if (cheng_v3_loop_label_depth > 0U) {
        cheng_v3_loop_label_depth -= 1U;
    }
}

static const char *v3_current_continue_label(void) {
    if (cheng_v3_loop_label_depth <= 0U) {
        return NULL;
    }
    return cheng_v3_loop_continue_labels[cheng_v3_loop_label_depth - 1U][0] != '\0'
               ? cheng_v3_loop_continue_labels[cheng_v3_loop_label_depth - 1U]
               : NULL;
}

static const char *v3_current_break_label(void) {
    if (cheng_v3_loop_label_depth <= 0U) {
        return NULL;
    }
    return cheng_v3_loop_break_labels[cheng_v3_loop_label_depth - 1U][0] != '\0'
               ? cheng_v3_loop_break_labels[cheng_v3_loop_label_depth - 1U]
               : NULL;
}

static int32_t v3_find_lowered_function_index_by_symbol(const V3LoweringPlanStub *lowering,
                                                        const char *symbol_text);
static bool v3_lowering_function_name_from_line(char *trimmed, char *out, size_t cap);
static void v3_parse_import_aliases_from_lines(char **lines,
                                               size_t line_count,
                                               V3ImportAlias *aliases,
                                               size_t *alias_count,
                                               size_t alias_cap);
static const char *v3_alias_module_path(const V3ImportAlias *aliases,
                                        size_t alias_count,
                                        const char *alias_text);
static bool v3_is_intrinsic_call_name(const char *name);
static bool v3_is_intrinsic_module_call(const char *module_path, const char *function_name);
static const V3LoweredFunctionStub *v3_find_entry_lowered_function(const V3LoweringPlanStub *lowering);
static void v3_source_path_to_module_path(const char *workspace_root,
                                          const char *package_root,
                                          const char *package_id,
                                          const char *source_path,
                                          char *out,
                                          size_t cap);
static void v3_primary_symbol_name(const V3SystemLinkPlanStub *plan,
                                   const V3LoweredFunctionStub *function,
                                   char *out,
                                   size_t cap);
static bool v3_abi_class_scalar_or_ptr(const char *abi_class);

static int32_t v3_line_indent(const char *line) {
    int32_t indent = 0;
    while (*line == ' ') {
        indent += 1;
        line++;
    }
    return indent;
}

static bool v3_collect_statement_from_lines(char **lines,
                                            size_t line_count,
                                            size_t *index_io,
                                            char *out,
                                            size_t out_cap) {
    size_t index = *index_io;
    size_t used = 0U;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    int32_t brace_depth = 0;
    bool in_string = false;
    if (index >= line_count) {
        return false;
    }
    out[0] = '\0';
    while (index < line_count) {
        char piece[4096];
        char *trimmed = v3_trim_inplace(lines[index]);
        size_t i;
        if (*trimmed == '\0') {
            index += 1U;
            if (used > 0U &&
                paren_depth <= 0 &&
                bracket_depth <= 0 &&
                brace_depth <= 0) {
                break;
            }
            continue;
        }
        if (used > 0U) {
            snprintf(out + used, out_cap - used, " ");
            used = strlen(out);
        }
        snprintf(piece, sizeof(piece), "%s", trimmed);
        snprintf(out + used, out_cap - used, "%s", piece);
        used = strlen(out);
        for (i = 0; piece[i] != '\0'; ++i) {
            if (piece[i] == '"') {
                in_string = !in_string;
                continue;
            }
            if (in_string) {
                continue;
            }
            if (piece[i] == '(') {
                paren_depth += 1;
            } else if (piece[i] == ')') {
                paren_depth -= 1;
            } else if (piece[i] == '[') {
                bracket_depth += 1;
            } else if (piece[i] == ']') {
                bracket_depth -= 1;
            } else if (piece[i] == '{') {
                brace_depth += 1;
            } else if (piece[i] == '}') {
                brace_depth -= 1;
            }
        }
        index += 1U;
        if (paren_depth <= 0 &&
            bracket_depth <= 0 &&
            brace_depth <= 0 &&
            !(used >= 2U &&
              (strcmp(out + used - 2U, "||") == 0 ||
               strcmp(out + used - 2U, "&&") == 0 ||
               strcmp(out + used - 2U, "==") == 0 ||
               strcmp(out + used - 2U, "!=") == 0 ||
               strcmp(out + used - 2U, "<=") == 0 ||
               strcmp(out + used - 2U, ">=") == 0 ||
               strcmp(out + used - 2U, "<<") == 0 ||
               strcmp(out + used - 2U, ">>") == 0)) &&
            !(used >= 1U &&
              (out[used - 1U] == '|' ||
               out[used - 1U] == '^' ||
               out[used - 1U] == '&' ||
               out[used - 1U] == '+' ||
               out[used - 1U] == '-' ||
               out[used - 1U] == '*' ||
               out[used - 1U] == '/' ||
               out[used - 1U] == '%' ||
               out[used - 1U] == ','))) {
            break;
        }
    }
    *index_io = index;
    return used > 0U;
}

static size_t v3_count_function_body_statements(char **lines,
                                                size_t line_count,
                                                size_t signature_end_index) {
    size_t index = signature_end_index + 1U;
    size_t count = 0U;
    while (index < line_count) {
        char raw_copy[4096];
        char statement[8192];
        char *trimmed;
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[index]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            index += 1U;
            continue;
        }
        if (v3_line_indent(lines[index]) <= 0 && v3_startswith(trimmed, "fn ")) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, &index, statement, sizeof(statement))) {
            break;
        }
        count += 1U;
    }
    return count;
}

static bool v3_is_last_logical_statement_in_function(char **lines,
                                                     size_t line_count,
                                                     size_t index_after_statement) {
    size_t index = index_after_statement;
    while (index < line_count) {
        char raw_copy[4096];
        char statement[8192];
        char *trimmed;
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[index]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            index += 1U;
            continue;
        }
        if (v3_line_indent(lines[index]) <= 0 && v3_startswith(trimmed, "fn ")) {
            return true;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, &index, statement, sizeof(statement))) {
            break;
        }
        return false;
    }
    return true;
}

static bool v3_const_is_whitespace_char(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static bool v3_const_str_strip_ascii(const char *text, char *out, size_t cap) {
    const char *start = text;
    const char *end = text + strlen(text);
    while (*start != '\0' && v3_const_is_whitespace_char(*start)) {
        start++;
    }
    while (end > start && v3_const_is_whitespace_char(end[-1])) {
        end--;
    }
    snprintf(out, cap, "%.*s", (int)(end - start), start);
    return true;
}

static bool v3_const_os_is_absolute(const char *text) {
    size_t len = strlen(text);
    if (len <= 0U || text[0] == '\0') {
        return false;
    }
    if (text[0] == '/' || text[0] == '\\') {
        return true;
    }
    if (len > 1U && text[1] == ':') {
        return true;
    }
    return false;
}

static bool v3_const_os_join_path(const char *a, const char *b, char *out, size_t cap) {
    size_t a_len = strlen(a);
    size_t b_len = strlen(b);
    if (a_len <= 0U || a[0] == '\0') {
        snprintf(out, cap, "%s", b);
        return true;
    }
    if (b_len <= 0U || b[0] == '\0') {
        snprintf(out, cap, "%s", a);
        return true;
    }
    if (a[a_len - 1U] == '/' || a[a_len - 1U] == '\\') {
        snprintf(out, cap, "%s%s", a, b);
        return true;
    }
    snprintf(out, cap, "%s/%s", a, b);
    return true;
}

static bool v3_const_parse_function_signature(const char *line,
                                              const char *function_name,
                                              char param_names[][128],
                                              size_t *param_count,
                                              size_t param_cap) {
    const char *open_paren;
    const char *close_paren;
    char params[4096];
    char items[32][4096];
    size_t item_count = 0U;
    size_t i;
    *param_count = 0U;
    open_paren = strchr(line, '(');
    close_paren = strchr(line, ')');
    if (!open_paren || !close_paren || close_paren <= open_paren) {
        return false;
    }
    char header_copy[4096];
    snprintf(header_copy, sizeof(header_copy), "%s", line);
    if (!v3_startswith(v3_trim_inplace(header_copy), "fn ")) {
        return false;
    }
    if (strstr(line, function_name) == NULL) {
        return false;
    }
    snprintf(params, sizeof(params), "%.*s", (int)(close_paren - open_paren - 1), open_paren + 1);
    if (!v3_split_top_level_args(params, items, &item_count, 32U)) {
        return false;
    }
    for (i = 0; i < item_count; ++i) {
        char *colon = strchr(items[i], ':');
        if (*param_count >= param_cap) {
            return false;
        }
        if (colon) {
            *colon = '\0';
        }
        v3_trim_copy_text(items[i], param_names[*param_count], sizeof(param_names[*param_count]));
        if (param_names[*param_count][0] != '\0') {
            *param_count += 1U;
        }
    }
    return true;
}

static const char *v3_type_abi_class(const char *type_text) {
    if (!type_text || *type_text == '\0' || strcmp(type_text, "void") == 0) {
        return "void";
    }
    if (strcmp(type_text, "int32") == 0 ||
        strcmp(type_text, "int") == 0 ||
        strcmp(type_text, "uint32") == 0 ||
        strcmp(type_text, "char") == 0) {
        return "i32";
    }
    if (strcmp(type_text, "int64") == 0 ||
        strcmp(type_text, "uint64") == 0) {
        return "i64";
    }
    if (strcmp(type_text, "bool") == 0) {
        return "bool";
    }
    if (strcmp(type_text, "ptr") == 0 ||
        strcmp(type_text, "cstring") == 0) {
        return "ptr";
    }
    return "composite";
}

static bool v3_strip_var_type_text(const char *type_text,
                                   char *out,
                                   size_t cap) {
    char trimmed[256];
    if (!type_text || !out || cap == 0U) {
        return false;
    }
    v3_trim_copy_text(type_text, trimmed, sizeof(trimmed));
    if (!v3_startswith(trimmed, "var ")) {
        snprintf(out, cap, "%s", trimmed);
        return false;
    }
    v3_trim_copy_text(trimmed + 4, out, cap);
    return out[0] != '\0';
}

static bool v3_parse_top_level_const_binding(const char *statement,
                                             char *name_out,
                                             size_t name_cap,
                                             char *type_out,
                                             size_t type_cap,
                                             char *expr_out,
                                             size_t expr_cap) {
    const char *eq = strchr(statement, '=');
    char left[256];
    char right[4096];
    char *colon;
    if (!eq) {
        return false;
    }
    snprintf(left, sizeof(left), "%.*s", (int)(eq - statement), statement);
    snprintf(right, sizeof(right), "%s", eq + 1);
    v3_trim_copy_text(left, left, sizeof(left));
    v3_trim_copy_text(right, expr_out, expr_cap);
    if (left[0] == '\0' || expr_out[0] == '\0') {
        return false;
    }
    colon = strchr(left, ':');
    if (colon) {
        *colon = '\0';
        snprintf(type_out, type_cap, "%s", colon + 1);
        v3_trim_copy_text(type_out, type_out, type_cap);
    } else {
        type_out[0] = '\0';
    }
    v3_trim_copy_text(left, name_out, name_cap);
    return name_out[0] != '\0';
}

static int32_t v3_find_top_level_const_index_by_symbol(const V3LoweringPlanStub *lowering,
                                                       const char *symbol_text) {
    size_t i;
    for (i = 0; i < lowering->const_count; ++i) {
        if (strcmp(lowering->consts[i].symbol_text, symbol_text) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool v3_try_parse_scalar_const_literal(const char *expr_text,
                                              const char *declared_type,
                                              const V3LoweringPlanStub *lowering,
                                              const char *owner_module_path,
                                              const V3ImportAlias *aliases,
                                              size_t alias_count,
                                              V3TopLevelConstStub *out) {
    char expr[4096];
    char *end = NULL;
    const char *declared_abi = v3_type_abi_class(declared_type);
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (strcmp(expr, "true") == 0 || strcmp(expr, "false") == 0) {
        snprintf(out->abi_class, sizeof(out->abi_class), "%s", "bool");
        out->bool_value = strcmp(expr, "true") == 0;
        out->i64_value = out->bool_value ? 1 : 0;
        return true;
    }
    if ((expr[0] >= '0' && expr[0] <= '9') ||
        (expr[0] == '-' && ((expr[1] >= '0' && expr[1] <= '9') || expr[1] == '0'))) {
        long long value = strtoll(expr, &end, 0);
        if (end && *v3_trim_inplace(end) == '\0') {
            snprintf(out->abi_class,
                     sizeof(out->abi_class),
                     "%s",
                     strcmp(declared_abi, "i64") == 0 ? "i64" : "i32");
            out->i64_value = (int64_t)value;
            return true;
        }
    }
    if (strchr(expr, '(') == NULL && strchr(expr, '[') == NULL && strchr(expr, ' ') == NULL) {
        char symbol_text[PATH_MAX];
        const V3TopLevelConstStub *existing = NULL;
        if (strchr(expr, '.') != NULL) {
            char alias_text[128];
            char const_name[128];
            char expr_copy[4096];
            char *dot;
            const char *module_path;
            snprintf(expr_copy, sizeof(expr_copy), "%s", expr);
            dot = strchr(expr_copy, '.');
            *dot = '\0';
            snprintf(alias_text, sizeof(alias_text), "%s", expr_copy);
            snprintf(const_name, sizeof(const_name), "%s", dot + 1);
            module_path = v3_alias_module_path(aliases, alias_count, alias_text);
            if (!module_path) {
                return false;
            }
            snprintf(symbol_text, sizeof(symbol_text), "%s::%s", module_path, const_name);
        } else {
            snprintf(symbol_text, sizeof(symbol_text), "%s::%s", owner_module_path, expr);
        }
        {
            int32_t index = v3_find_top_level_const_index_by_symbol(lowering, symbol_text);
            if (index < 0) {
                return false;
            }
            existing = &lowering->consts[index];
        }
        snprintf(out->abi_class, sizeof(out->abi_class), "%s", existing->abi_class);
        out->i64_value = existing->i64_value;
        out->bool_value = existing->bool_value;
        if (strcmp(declared_abi, "i64") == 0 && strcmp(out->abi_class, "i32") == 0) {
            snprintf(out->abi_class, sizeof(out->abi_class), "%s", "i64");
        }
        return v3_abi_class_scalar_or_ptr(out->abi_class);
    }
    return false;
}

static bool v3_collect_top_level_consts_from_source(const V3SystemLinkPlanStub *plan,
                                                    const char *source_path,
                                                    V3LoweringPlanStub *lowering) {
    char owner_module_path[PATH_MAX];
    V3ImportAlias aliases[CHENG_V3_MAX_IMPORT_ALIASES];
    size_t alias_count = 0U;
    char *owned;
    char *cursor;
    char *line;
    char *lines[CHENG_V3_MAX_PLAN_FUNCTIONS];
    size_t line_count = 0U;
    size_t i;
    bool in_const_block = false;
    v3_source_path_to_module_path(plan->workspace_root,
                                  plan->package_root,
                                  plan->package_id,
                                  source_path,
                                  owner_module_path,
                                  sizeof(owner_module_path));
    owned = v3_read_file(source_path);
    if (!owned) {
        fprintf(stderr, "[cheng_v3_seed] type scan read failed: %s\n", source_path);
        return false;
    }
    cursor = owned;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        if (line_count >= CHENG_V3_MAX_PLAN_FUNCTIONS) {
            fprintf(stderr, "[cheng_v3_seed] type scan line overflow: %s\n", source_path);
            free(owned);
            return false;
        }
        lines[line_count++] = line;
    }
    v3_parse_import_aliases_from_lines(lines,
                                       line_count,
                                       aliases,
                                       &alias_count,
                                       CHENG_V3_MAX_IMPORT_ALIASES);
    for (i = 0; i < line_count; ++i) {
        char line_copy[4096];
        char name[128];
        char type_text[128];
        char expr_text[4096];
        V3TopLevelConstStub value;
        int32_t indent;
        char *trimmed;
        snprintf(line_copy, sizeof(line_copy), "%s", lines[i]);
        indent = v3_line_indent(line_copy);
        trimmed = v3_trim_inplace(line_copy);
        if (*trimmed == '\0') {
            continue;
        }
        if (indent <= 0 && strcmp(trimmed, "const") == 0) {
            in_const_block = true;
            continue;
        }
        if (!in_const_block) {
            continue;
        }
        if (indent <= 0) {
            in_const_block = false;
            continue;
        }
        if (!v3_parse_top_level_const_binding(trimmed,
                                              name,
                                              sizeof(name),
                                              type_text,
                                              sizeof(type_text),
                                              expr_text,
                                              sizeof(expr_text))) {
            continue;
        }
        if (!v3_try_parse_scalar_const_literal(expr_text,
                                               type_text,
                                               lowering,
                                               owner_module_path,
                                               aliases,
                                               alias_count,
                                               &value)) {
            continue;
        }
        if (lowering->const_count >= CHENG_V3_MAX_TOP_LEVEL_CONSTS) {
            free(owned);
            return false;
        }
        snprintf(lowering->consts[lowering->const_count].symbol_text,
                 sizeof(lowering->consts[lowering->const_count].symbol_text),
                 "%s::%s",
                 owner_module_path,
                 name);
        snprintf(lowering->consts[lowering->const_count].owner_module_path,
                 sizeof(lowering->consts[lowering->const_count].owner_module_path),
                 "%s",
                 owner_module_path);
        snprintf(lowering->consts[lowering->const_count].const_name,
                 sizeof(lowering->consts[lowering->const_count].const_name),
                 "%s",
                 name);
        snprintf(lowering->consts[lowering->const_count].type_text,
                 sizeof(lowering->consts[lowering->const_count].type_text),
                 "%s",
                 type_text[0] != '\0' ? type_text :
                 (strcmp(value.abi_class, "i64") == 0 ? "int64" :
                  (strcmp(value.abi_class, "bool") == 0 ? "bool" : "int32")));
        snprintf(lowering->consts[lowering->const_count].abi_class,
                 sizeof(lowering->consts[lowering->const_count].abi_class),
                 "%s",
                 value.abi_class);
        lowering->consts[lowering->const_count].i64_value = value.i64_value;
        lowering->consts[lowering->const_count].bool_value = value.bool_value;
        lowering->const_count += 1U;
    }
    free(owned);
    return true;
}

static const V3TopLevelConstStub *v3_find_expr_top_level_const(const V3LoweringPlanStub *lowering,
                                                               const char *owner_module_path,
                                                               const V3ImportAlias *aliases,
                                                               size_t alias_count,
                                                               const char *expr_text) {
    char expr[4096];
    char symbol_text[PATH_MAX];
    int32_t index;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0' || strchr(expr, '(') != NULL || strchr(expr, '[') != NULL) {
        return NULL;
    }
    if (strchr(expr, '.') != NULL) {
        char alias_text[128];
        char const_name[128];
        char expr_copy[4096];
        char *dot;
        const char *module_path;
        snprintf(expr_copy, sizeof(expr_copy), "%s", expr);
        dot = strchr(expr_copy, '.');
        *dot = '\0';
        snprintf(alias_text, sizeof(alias_text), "%s", expr_copy);
        snprintf(const_name, sizeof(const_name), "%s", dot + 1);
        module_path = v3_alias_module_path(aliases, alias_count, alias_text);
        if (!module_path) {
            return NULL;
        }
        snprintf(symbol_text, sizeof(symbol_text), "%s::%s", module_path, const_name);
    } else {
        snprintf(symbol_text, sizeof(symbol_text), "%s::%s", owner_module_path, expr);
    }
    index = v3_find_top_level_const_index_by_symbol(lowering, symbol_text);
    return index >= 0 ? &lowering->consts[index] : NULL;
}

static int32_t v3_align_up_i32(int32_t value, int32_t align) {
    if (align <= 1) {
        return value;
    }
    return (value + align - 1) / align * align;
}

static void v3_strip_var_prefix(const char *type_text, char *out, size_t cap) {
    char scratch[256];
    v3_trim_copy_text(type_text, scratch, sizeof(scratch));
    if (v3_startswith(scratch, "var ")) {
        v3_trim_copy_text(scratch + 4, out, cap);
        return;
    }
    v3_copy_text(out, cap, scratch);
}

static bool v3_eval_type_int32_expr(const V3LoweringPlanStub *lowering,
                                    const char *owner_module_path,
                                    const V3ImportAlias *aliases,
                                    size_t alias_count,
                                    const char *expr_text,
                                    int32_t *value_out) {
    char expr[256];
    char *end = NULL;
    long long parsed;
    const V3TopLevelConstStub *top_level_const;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return false;
    }
    parsed = strtoll(expr, &end, 0);
    if (end && *v3_trim_inplace(end) == '\0') {
        *value_out = (int32_t)parsed;
        return true;
    }
    top_level_const = v3_find_expr_top_level_const(lowering,
                                                   owner_module_path,
                                                   aliases,
                                                   alias_count,
                                                   expr);
    if (!top_level_const) {
        return false;
    }
    *value_out = (int32_t)top_level_const->i64_value;
    return true;
}

static bool v3_normalize_type_text(const V3LoweringPlanStub *lowering,
                                   const char *owner_module_path,
                                   const V3ImportAlias *aliases,
                                   size_t alias_count,
                                   const char *type_text,
                                   char *out,
                                   size_t cap) {
    char trimmed[256];
    char inner[256];
    char normalized_inner[256];
    char expr[256];
    char *open;
    char *close;
    int32_t fixed_len = 0;
    v3_strip_var_prefix(type_text, trimmed, sizeof(trimmed));
    if (trimmed[0] == '\0') {
        return false;
    }
    if (strcmp(trimmed, "void") == 0 ||
        strcmp(trimmed, "int32") == 0 ||
        strcmp(trimmed, "int") == 0 ||
        strcmp(trimmed, "uint32") == 0 ||
        strcmp(trimmed, "char") == 0 ||
        strcmp(trimmed, "int64") == 0 ||
        strcmp(trimmed, "uint64") == 0 ||
        strcmp(trimmed, "bool") == 0 ||
        strcmp(trimmed, "ptr") == 0 ||
        strcmp(trimmed, "cstring") == 0 ||
        strcmp(trimmed, "str") == 0 ||
        strcmp(trimmed, "Bytes") == 0) {
        v3_copy_text(out, cap, trimmed);
        return true;
    }
    if (v3_startswith(trimmed, "Result[") && trimmed[strlen(trimmed) - 1U] == ']') {
        snprintf(inner, sizeof(inner), "%.*s", (int)(strlen(trimmed) - 8U), trimmed + 7);
        if (!v3_normalize_type_text(lowering,
                                    owner_module_path,
                                    aliases,
                                    alias_count,
                                    inner,
                                    normalized_inner,
                                    sizeof(normalized_inner))) {
            return false;
        }
        snprintf(out, cap, "Result[%s]", normalized_inner);
        return true;
    }
    if (strlen(trimmed) > 2U && strcmp(trimmed + strlen(trimmed) - 2U, "[]") == 0) {
        snprintf(inner, sizeof(inner), "%.*s", (int)(strlen(trimmed) - 2U), trimmed);
        if (!v3_normalize_type_text(lowering,
                                    owner_module_path,
                                    aliases,
                                    alias_count,
                                    inner,
                                    normalized_inner,
                                    sizeof(normalized_inner))) {
            return false;
        }
        snprintf(out, cap, "%s[]", normalized_inner);
        return true;
    }
    open = strrchr(trimmed, '[');
    close = strrchr(trimmed, ']');
    if (open && close && close > open && close[1] == '\0') {
        snprintf(inner, sizeof(inner), "%.*s", (int)(open - trimmed), trimmed);
        snprintf(expr, sizeof(expr), "%.*s", (int)(close - open - 1), open + 1);
        if (!v3_normalize_type_text(lowering,
                                    owner_module_path,
                                    aliases,
                                    alias_count,
                                    inner,
                                    normalized_inner,
                                    sizeof(normalized_inner)) ||
            !v3_eval_type_int32_expr(lowering,
                                     owner_module_path,
                                     aliases,
                                     alias_count,
                                     expr,
                                     &fixed_len)) {
            return false;
        }
        snprintf(out, cap, "%s[%d]", normalized_inner, fixed_len);
        return true;
    }
    if (strstr(trimmed, "::") != NULL) {
        v3_copy_text(out, cap, trimmed);
        return true;
    }
    if (strchr(trimmed, '.') != NULL) {
        char alias_text[128];
        char type_name[128];
        char copy[256];
        char *dot;
        const char *module_path;
        snprintf(copy, sizeof(copy), "%s", trimmed);
        dot = strchr(copy, '.');
        *dot = '\0';
        snprintf(alias_text, sizeof(alias_text), "%s", copy);
        snprintf(type_name, sizeof(type_name), "%s", dot + 1);
        module_path = v3_alias_module_path(aliases, alias_count, alias_text);
        if (!module_path) {
            return false;
        }
        snprintf(out, cap, "%s::%s", module_path, type_name);
        return true;
    }
    snprintf(out, cap, "%s::%s", owner_module_path, trimmed);
    return true;
}

static int32_t v3_find_type_def_index_by_symbol(const V3LoweringPlanStub *lowering,
                                                const char *owner_module_path,
                                                const char *type_name) {
    size_t i;
    for (i = 0; i < lowering->type_def_count; ++i) {
        if (strcmp(lowering->type_defs[i].owner_module_path, owner_module_path) == 0 &&
            strcmp(lowering->type_defs[i].type_name, type_name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool v3_collect_type_defs_from_source(const V3SystemLinkPlanStub *plan,
                                             const char *source_path,
                                             V3LoweringPlanStub *lowering) {
    char owner_module_path[PATH_MAX];
    V3ImportAlias aliases[CHENG_V3_MAX_IMPORT_ALIASES];
    size_t alias_count = 0U;
    char *owned;
    char *cursor;
    char *line;
    char *lines[CHENG_V3_MAX_PLAN_FUNCTIONS];
    size_t line_count = 0U;
    size_t i = 0U;
    bool in_type_block = false;
    v3_source_path_to_module_path(plan->workspace_root,
                                  plan->package_root,
                                  plan->package_id,
                                  source_path,
                                  owner_module_path,
                                  sizeof(owner_module_path));
    owned = v3_read_file(source_path);
    if (!owned) {
        return false;
    }
    cursor = owned;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        if (line_count >= CHENG_V3_MAX_PLAN_FUNCTIONS) {
            free(owned);
            return false;
        }
        lines[line_count++] = line;
    }
    v3_parse_import_aliases_from_lines(lines,
                                       line_count,
                                       aliases,
                                       &alias_count,
                                       CHENG_V3_MAX_IMPORT_ALIASES);
    while (i < line_count) {
        char line_copy[4096];
        char *trimmed;
        int32_t indent;
        snprintf(line_copy, sizeof(line_copy), "%s", lines[i]);
        indent = v3_line_indent(line_copy);
        trimmed = v3_trim_inplace(line_copy);
        if (*trimmed == '\0' || *trimmed == '#') {
            i += 1U;
            continue;
        }
        if (indent == 0 && strcmp(trimmed, "type") == 0) {
            in_type_block = true;
            i += 1U;
            continue;
        }
        if (!in_type_block) {
            i += 1U;
            continue;
        }
        if (indent == 0) {
            in_type_block = false;
            continue;
        }
        if (strchr(trimmed, '=') != NULL) {
            char name[128];
            char rhs[256];
            char normalized_rhs[256];
            int32_t header_indent = indent;
            char *eq = strchr(trimmed, '=');
            V3TypeDefStub *type_def;
            *eq = '\0';
            snprintf(name, sizeof(name), "%s", trimmed);
            snprintf(rhs, sizeof(rhs), "%s", eq + 1);
            v3_trim_copy_text(name, name, sizeof(name));
            v3_trim_copy_text(rhs, rhs, sizeof(rhs));
            if (name[0] == '\0' ||
                v3_find_type_def_index_by_symbol(lowering, owner_module_path, name) >= 0 ||
                lowering->type_def_count >= CHENG_V3_MAX_TYPE_DEFS) {
                fprintf(stderr, "[cheng_v3_seed] invalid or duplicate type head: %s line=%zu text=%s\n",
                        source_path,
                        i + 1U,
                        lines[i]);
                free(owned);
                return false;
            }
            type_def = &lowering->type_defs[lowering->type_def_count];
            memset(type_def, 0, sizeof(*type_def));
            snprintf(type_def->owner_module_path, sizeof(type_def->owner_module_path), "%s", owner_module_path);
            snprintf(type_def->type_name, sizeof(type_def->type_name), "%s", name);
            if (rhs[0] != '\0') {
                if (strcmp(rhs, "enum") == 0) {
                    type_def->kind = V3_TYPE_DEF_ALIAS;
                    snprintf(type_def->alias_target, sizeof(type_def->alias_target), "%s", "int32");
                    lowering->type_def_count += 1U;
                    i += 1U;
                    while (i < line_count) {
                        char enum_line_copy[4096];
                        char *enum_trimmed;
                        int32_t enum_indent;
                        snprintf(enum_line_copy, sizeof(enum_line_copy), "%s", lines[i]);
                        enum_indent = v3_line_indent(enum_line_copy);
                        enum_trimmed = v3_trim_inplace(enum_line_copy);
                        if (*enum_trimmed == '\0') {
                            i += 1U;
                            continue;
                        }
                        if (enum_indent <= header_indent) {
                            break;
                        }
                        i += 1U;
                    }
                    continue;
                }
                if (strcmp(rhs, "object") == 0) {
                    rhs[0] = '\0';
                }
            }
            if (rhs[0] != '\0') {
                if (!v3_normalize_type_text(lowering,
                                            owner_module_path,
                                            aliases,
                                            alias_count,
                                            rhs,
                                            normalized_rhs,
                                            sizeof(normalized_rhs))) {
                    fprintf(stderr, "[cheng_v3_seed] type alias normalize failed: %s line=%zu text=%s\n",
                            source_path,
                            i + 1U,
                            lines[i]);
                    free(owned);
                    return false;
                }
                type_def->kind = V3_TYPE_DEF_ALIAS;
                snprintf(type_def->alias_target, sizeof(type_def->alias_target), "%s", normalized_rhs);
                lowering->type_def_count += 1U;
                i += 1U;
                continue;
            }
            type_def->kind = V3_TYPE_DEF_RECORD;
            i += 1U;
            while (i < line_count) {
                char field_line_copy[4096];
                char *field_trimmed;
                int32_t field_indent;
                char *colon;
                snprintf(field_line_copy, sizeof(field_line_copy), "%s", lines[i]);
                field_indent = v3_line_indent(field_line_copy);
                field_trimmed = v3_trim_inplace(field_line_copy);
                if (*field_trimmed == '\0' || *field_trimmed == '#') {
                    i += 1U;
                    continue;
                }
                if (field_indent <= header_indent) {
                    break;
                }
                colon = strchr(field_trimmed, ':');
                if (!colon || type_def->field_count >= CHENG_V3_MAX_TYPE_FIELDS) {
                    fprintf(stderr, "[cheng_v3_seed] invalid type field: %s line=%zu text=%s\n",
                            source_path,
                            i + 1U,
                            lines[i]);
                    free(owned);
                    return false;
                }
                *colon = '\0';
                snprintf(type_def->fields[type_def->field_count].name,
                         sizeof(type_def->fields[type_def->field_count].name),
                         "%s",
                         v3_trim_inplace(field_trimmed));
                if (type_def->fields[type_def->field_count].name[0] == '\0') {
                    fprintf(stderr, "[cheng_v3_seed] empty type field name: %s line=%zu\n",
                            source_path,
                            i + 1U);
                    free(owned);
                    return false;
                }
                if (!v3_normalize_type_text(lowering,
                                            owner_module_path,
                                            aliases,
                                            alias_count,
                                            colon + 1,
                                            type_def->fields[type_def->field_count].type_text,
                                            sizeof(type_def->fields[type_def->field_count].type_text))) {
                    fprintf(stderr, "[cheng_v3_seed] type field normalize failed: %s line=%zu text=%s\n",
                            source_path,
                            i + 1U,
                            lines[i]);
                    free(owned);
                    return false;
                }
                type_def->field_count += 1U;
                i += 1U;
            }
            lowering->type_def_count += 1U;
            continue;
        }
        fprintf(stderr, "[cheng_v3_seed] malformed type block: %s line=%zu text=%s\n",
                source_path,
                i + 1U,
                lines[i]);
        free(owned);
        return false;
    }
    free(owned);
    return true;
}

static bool v3_parse_fixed_array_type(const char *type_text,
                                      char *elem_type_out,
                                      size_t elem_cap,
                                      int32_t *len_out) {
    char copy[256];
    char *open;
    char *close;
    if (v3_startswith(type_text, "Result[") || strlen(type_text) <= 3U) {
        return false;
    }
    snprintf(copy, sizeof(copy), "%s", type_text);
    open = strrchr(copy, '[');
    close = strrchr(copy, ']');
    if (!open || !close || close <= open || close[1] != '\0') {
        return false;
    }
    *open = '\0';
    if (strchr(open + 1, '[') != NULL) {
        return false;
    }
    v3_trim_copy_text(copy, elem_type_out, elem_cap);
    *len_out = (int32_t)strtol(open + 1, NULL, 10);
    return elem_type_out[0] != '\0' && *len_out >= 0;
}

static bool v3_compute_type_layout_impl(const V3LoweringPlanStub *lowering,
                                        const char *type_text,
                                        V3TypeLayoutStub *layout_out,
                                        size_t depth) {
    char stripped[256];
    char inner[256];
    char owner_module[PATH_MAX];
    char type_name[128];
    int32_t fixed_len = 0;
    int32_t found = -1;
    size_t i;
    if (depth > 64U) {
        return false;
    }
    v3_strip_var_prefix(type_text, stripped, sizeof(stripped));
    memset(layout_out, 0, sizeof(*layout_out));
    if (strcmp(stripped, "bool") == 0) {
        layout_out->size = 1;
        layout_out->align = 1;
        return true;
    }
    if (strcmp(stripped, "int32") == 0 ||
        strcmp(stripped, "int") == 0 ||
        strcmp(stripped, "uint32") == 0 ||
        strcmp(stripped, "char") == 0) {
        layout_out->size = 4;
        layout_out->align = 4;
        return true;
    }
    if (strcmp(stripped, "int64") == 0 ||
        strcmp(stripped, "uint64") == 0 ||
        strcmp(stripped, "ptr") == 0 ||
        strcmp(stripped, "cstring") == 0) {
        layout_out->size = 8;
        layout_out->align = 8;
        return true;
    }
    if (strcmp(stripped, "str") == 0) {
        layout_out->size = 24;
        layout_out->align = 8;
        return true;
    }
    if (strcmp(stripped, "Bytes") == 0) {
        layout_out->size = 16;
        layout_out->align = 8;
        return true;
    }
    if (strlen(stripped) > 2U && strcmp(stripped + strlen(stripped) - 2U, "[]") == 0) {
        layout_out->size = 16;
        layout_out->align = 8;
        return true;
    }
    if (v3_startswith(stripped, "Result[") && stripped[strlen(stripped) - 1U] == ']') {
        V3TypeLayoutStub value_layout;
        int32_t value_offset;
        int32_t err_offset;
        snprintf(inner, sizeof(inner), "%.*s", (int)(strlen(stripped) - 8U), stripped + 7);
        if (!v3_compute_type_layout_impl(lowering, inner, &value_layout, depth + 1U)) {
            return false;
        }
        value_offset = v3_align_up_i32(1, value_layout.align);
        err_offset = v3_align_up_i32(value_offset + value_layout.size, 8);
        layout_out->align = value_layout.align > 8 ? value_layout.align : 8;
        layout_out->size = v3_align_up_i32(err_offset + 32, layout_out->align);
        return true;
    }
    if (v3_parse_fixed_array_type(stripped, inner, sizeof(inner), &fixed_len)) {
        V3TypeLayoutStub elem_layout;
        if (!v3_compute_type_layout_impl(lowering, inner, &elem_layout, depth + 1U)) {
            return false;
        }
        layout_out->align = elem_layout.align;
        layout_out->size = v3_align_up_i32(elem_layout.size * fixed_len, elem_layout.align);
        return true;
    }
    if (strstr(stripped, "::") == NULL) {
        return false;
    }
    snprintf(owner_module, sizeof(owner_module), "%s", stripped);
    {
        char *sep = strrchr(owner_module, ':');
        if (!sep || sep == owner_module || sep[-1] != ':') {
            return false;
        }
        sep[-1] = '\0';
        snprintf(type_name, sizeof(type_name), "%s", sep + 1);
    }
    found = v3_find_type_def_index_by_symbol(lowering, owner_module, type_name);
    if (found < 0) {
        return false;
    }
    if (lowering->type_defs[found].kind == V3_TYPE_DEF_ALIAS) {
        return v3_compute_type_layout_impl(lowering,
                                           lowering->type_defs[found].alias_target,
                                           layout_out,
                                           depth + 1U);
    }
    layout_out->align = 1;
    layout_out->size = 0;
    for (i = 0; i < lowering->type_defs[found].field_count; ++i) {
        V3TypeLayoutStub field_layout;
        int32_t field_align;
        if (!v3_compute_type_layout_impl(lowering,
                                         lowering->type_defs[found].fields[i].type_text,
                                         &field_layout,
                                         depth + 1U)) {
            return false;
        }
        field_align = field_layout.align > 0 ? field_layout.align : 1;
        layout_out->size = v3_align_up_i32(layout_out->size, field_align);
        layout_out->size += field_layout.size;
        if (field_align > layout_out->align) {
            layout_out->align = field_align;
        }
    }
    layout_out->size = v3_align_up_i32(layout_out->size, layout_out->align);
    return true;
}

static bool v3_parse_function_signature_meta(const char *line,
                                             const char *function_name,
                                             V3LoweredFunctionStub *function) {
    const char *open_paren;
    const char *close_paren;
    const char *eq;
    char header_copy[4096];
    char params[4096];
    char items[32][4096];
    char return_part[256];
    size_t item_count = 0U;
    size_t i;
    open_paren = strchr(line, '(');
    close_paren = strchr(line, ')');
    eq = strrchr(line, '=');
    if (!open_paren || !close_paren || close_paren <= open_paren || !eq || eq <= close_paren) {
        return false;
    }
    snprintf(header_copy, sizeof(header_copy), "%s", line);
    if (!v3_startswith(v3_trim_inplace(header_copy), "fn ")) {
        return false;
    }
    if (strstr(line, function_name) == NULL) {
        return false;
    }
    function->param_count = 0U;
    function->return_type[0] = '\0';
    snprintf(function->return_abi_class, sizeof(function->return_abi_class), "%s", "void");
    snprintf(params, sizeof(params), "%.*s", (int)(close_paren - open_paren - 1), open_paren + 1);
    if (!v3_split_top_level_args(params, items, &item_count, 32U)) {
        return false;
    }
    for (i = 0; i < item_count; ++i) {
        char *colon = strchr(items[i], ':');
        char name_text[128];
        char type_text[128];
        char value_type_text[128];
        if (function->param_count >= 32U) {
            return false;
        }
        name_text[0] = '\0';
        type_text[0] = '\0';
        value_type_text[0] = '\0';
        if (colon) {
            *colon = '\0';
            snprintf(name_text, sizeof(name_text), "%s", items[i]);
            snprintf(type_text, sizeof(type_text), "%s", colon + 1);
        } else {
            snprintf(name_text, sizeof(name_text), "%s", items[i]);
            snprintf(type_text, sizeof(type_text), "%s", "unknown");
        }
        v3_trim_copy_text(name_text,
                          function->param_names[function->param_count],
                          sizeof(function->param_names[function->param_count]));
        function->param_is_var[function->param_count] =
            v3_strip_var_type_text(type_text, value_type_text, sizeof(value_type_text));
        if (function->param_is_var[function->param_count]) {
            v3_trim_copy_text(value_type_text,
                              function->param_types[function->param_count],
                              sizeof(function->param_types[function->param_count]));
        } else {
            v3_trim_copy_text(type_text,
                              function->param_types[function->param_count],
                              sizeof(function->param_types[function->param_count]));
        }
        snprintf(function->param_abi_classes[function->param_count],
                 sizeof(function->param_abi_classes[function->param_count]),
                 "%s",
                 v3_type_abi_class(function->param_types[function->param_count]));
        function->param_count += 1U;
    }
    return_part[0] = '\0';
    snprintf(return_part, sizeof(return_part), "%.*s", (int)(eq - close_paren - 1), close_paren + 1);
    v3_trim_copy_text(return_part, return_part, sizeof(return_part));
    if (return_part[0] != '\0' && return_part[0] == ':') {
        memmove(return_part, return_part + 1, strlen(return_part));
        v3_trim_copy_text(return_part, function->return_type, sizeof(function->return_type));
    }
    if (function->return_type[0] == '\0') {
        snprintf(function->return_type, sizeof(function->return_type), "%s", "void");
    }
    snprintf(function->return_abi_class,
             sizeof(function->return_abi_class),
             "%s",
             v3_type_abi_class(function->return_type));
    return true;
}

static bool v3_signature_matches_lowered_function(const char *signature_text,
                                                  const V3LoweredFunctionStub *function) {
    V3LoweredFunctionStub probe;
    size_t i;
    memset(&probe, 0, sizeof(probe));
    if (!v3_parse_function_signature_meta(signature_text, function->function_name, &probe)) {
        return false;
    }
    if (probe.param_count != function->param_count) {
        return false;
    }
    if (strcmp(probe.return_type, function->return_type) != 0 ||
        strcmp(probe.return_abi_class, function->return_abi_class) != 0) {
        return false;
    }
    for (i = 0; i < function->param_count; ++i) {
        if (probe.param_is_var[i] != function->param_is_var[i] ||
            strcmp(probe.param_types[i], function->param_types[i]) != 0 ||
            strcmp(probe.param_abi_classes[i], function->param_abi_classes[i]) != 0) {
            return false;
        }
    }
    return true;
}

static void v3_lowered_function_abi_summary(const V3LoweredFunctionStub *function,
                                            char *out,
                                            size_t cap) {
    size_t i;
    if (cap <= 1U) {
        return;
    }
    out[0] = '\0';
    snprintf(out + strlen(out),
             cap - strlen(out),
             "ret=%s/%s params=",
             function->return_type[0] != '\0' ? function->return_type : "void",
             function->return_abi_class[0] != '\0' ? function->return_abi_class : "void");
    if (function->param_count <= 0U) {
        strncat(out, "-", cap - strlen(out) - 1U);
        return;
    }
    for (i = 0; i < function->param_count; ++i) {
        char chunk[256];
        if (i > 0U) {
            strncat(out, ";", cap - strlen(out) - 1U);
        }
        snprintf(chunk,
                 sizeof(chunk),
                 "%s:%s%s/%s",
                 function->param_names[i][0] != '\0' ? function->param_names[i] : "_",
                 function->param_is_var[i] ? "var " : "",
                 function->param_types[i][0] != '\0' ? function->param_types[i] : "unknown",
                 function->param_abi_classes[i][0] != '\0' ? function->param_abi_classes[i] : "unknown");
        strncat(out, chunk, cap - strlen(out) - 1U);
    }
}

static bool v3_collect_function_signature(char **lines,
                                          size_t line_count,
                                          size_t fn_line_index,
                                          char *signature_out,
                                          size_t signature_cap,
                                          size_t *signature_end_index_out) {
    size_t i;
    signature_out[0] = '\0';
    for (i = fn_line_index; i < line_count; ++i) {
        char line_copy[4096];
        char *trimmed;
        snprintf(line_copy, sizeof(line_copy), "%s", lines[i]);
        trimmed = v3_trim_inplace(line_copy);
        if (*trimmed == '\0') {
            continue;
        }
        if (i > fn_line_index && v3_startswith(trimmed, "fn ")) {
            return false;
        }
        if (i == fn_line_index &&
            strchr(trimmed, ')') != NULL &&
            strchr(trimmed, '=') == NULL) {
            return false;
        }
        if (signature_out[0] != '\0') {
            snprintf(signature_out + strlen(signature_out),
                     signature_cap - strlen(signature_out),
                     " ");
        }
        snprintf(signature_out + strlen(signature_out),
                 signature_cap - strlen(signature_out),
                 "%s",
                 trimmed);
        if (strchr(trimmed, '=') != NULL) {
            *signature_end_index_out = i;
            return true;
        }
    }
    return false;
}

static bool v3_load_function_source_lines(const V3LoweredFunctionStub *function,
                                          char ***lines_out,
                                          char **owned_out,
                                          size_t *line_count_out,
                                          size_t *fn_line_index_out,
                                          char param_names[][128],
                                          size_t *param_count_out,
                                          char owned_path[PATH_MAX]) {
    char *owned;
    char *cursor;
    char *line;
    char **lines;
    size_t line_count = 0U;
    size_t fn_line_index = 0U;
    bool found = false;
    size_t i;
    v3_copy_text(owned_path, PATH_MAX, function->source_path);
    owned = v3_read_file(function->source_path);
    if (!owned) {
        return false;
    }
    lines = (char **)v3_xmalloc(sizeof(char *) * CHENG_V3_MAX_PLAN_FUNCTIONS);
    cursor = owned;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        if (line_count >= CHENG_V3_MAX_PLAN_FUNCTIONS) {
            free(lines);
            free(owned);
            return false;
        }
        lines[line_count] = line;
        line_count += 1U;
    }
    for (i = 0; i < line_count; ++i) {
        char header_copy[4096];
        char function_name[128];
        snprintf(header_copy, sizeof(header_copy), "%s", lines[i]);
        if (v3_lowering_function_name_from_line(v3_trim_inplace(header_copy),
                                                function_name,
                                                sizeof(function_name)) &&
            strcmp(function_name, function->function_name) == 0) {
            char signature_text[8192];
            size_t signature_end_index = i;
            found = true;
            if (!v3_collect_function_signature(lines,
                                               line_count,
                                               i,
                                               signature_text,
                                               sizeof(signature_text),
                                               &signature_end_index) ||
                !v3_signature_matches_lowered_function(signature_text, function) ||
                !v3_const_parse_function_signature(signature_text,
                                                  function->function_name,
                                                  param_names,
                                                  param_count_out,
                                                  CHENG_V3_CONST_MAX_BINDINGS)) {
                found = false;
                continue;
            }
            fn_line_index = signature_end_index;
            break;
        }
    }
    if (!found) {
        free(lines);
        free(owned);
        return false;
    }
    *lines_out = lines;
    *owned_out = owned;
    *line_count_out = line_count;
    *fn_line_index_out = fn_line_index;
    return true;
}

static bool v3_const_eval_expr(const V3LoweringPlanStub *lowering,
                               const V3LoweredFunctionStub *current_function,
                               const V3ImportAlias *aliases,
                               size_t alias_count,
                               V3ConstEnv *env,
                               const char *expr_text,
                               V3ConstValue *out,
                               size_t depth);

static bool v3_const_eval_function(const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *function,
                                   const V3ConstValue *args,
                                   size_t arg_count,
                                   V3ConstValue *out,
                                   V3ConstEntryProgram *entry_program,
                                   size_t depth);

static bool v3_const_value_eq(const V3ConstValue *lhs,
                              const V3ConstValue *rhs,
                              bool *out) {
    if (lhs->kind != rhs->kind) {
        *out = false;
        return true;
    }
    switch (lhs->kind) {
        case V3_CONST_BOOL:
            *out = lhs->bool_value == rhs->bool_value;
            return true;
        case V3_CONST_I32:
            *out = lhs->i32_value == rhs->i32_value;
            return true;
        case V3_CONST_STR:
        case V3_CONST_SYMBOL:
            *out = strcmp(lhs->text_value ? lhs->text_value : "",
                          rhs->text_value ? rhs->text_value : "") == 0;
            return true;
        default:
            return false;
    }
}

static bool v3_const_record_new_from_named_args(const char args[][4096],
                                                size_t arg_count,
                                                const V3LoweringPlanStub *lowering,
                                                const V3LoweredFunctionStub *current_function,
                                                const V3ImportAlias *aliases,
                                                size_t alias_count,
                                                V3ConstEnv *env,
                                                V3ConstValue *out,
                                                size_t depth) {
    size_t i;
    V3ConstRecord *record = (V3ConstRecord *)v3_xmalloc(sizeof(V3ConstRecord));
    memset(record, 0, sizeof(*record));
    for (i = 0; i < arg_count; ++i) {
        int32_t colon = v3_find_top_level_binary_op(args[i], ":");
        char field_name[128];
        char expr[4096];
        if (colon <= 0 || record->field_count >= CHENG_V3_CONST_MAX_FIELDS) {
            v3_const_record_free(record);
            return false;
        }
        snprintf(field_name, sizeof(field_name), "%.*s", colon, args[i]);
        snprintf(expr, sizeof(expr), "%s", args[i] + colon + 1);
        v3_trim_copy_text(field_name, field_name, sizeof(field_name));
        v3_trim_copy_text(expr, expr, sizeof(expr));
        snprintf(record->field_names[record->field_count],
                 sizeof(record->field_names[record->field_count]),
                 "%s",
                 field_name);
        v3_const_value_init(&record->field_values[record->field_count]);
        if (!v3_const_eval_expr(lowering,
                                current_function,
                                aliases,
                                alias_count,
                                env,
                                expr,
                                &record->field_values[record->field_count],
                                depth + 1U)) {
            v3_const_record_free(record);
            return false;
        }
        record->field_count += 1U;
    }
    v3_const_value_free(out);
    out->kind = V3_CONST_RECORD;
    out->record_value = record;
    return true;
}

static bool v3_const_eval_intrinsic_call(const char *callee,
                                         const char args[][4096],
                                         size_t arg_count,
                                         const V3LoweringPlanStub *lowering,
                                         const V3LoweredFunctionStub *current_function,
                                         const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         V3ConstEnv *env,
                                         V3ConstValue *out,
                                         size_t depth) {
    V3ConstValue arg0;
    V3ConstValue arg1;
    char buffer[PATH_MAX];
    v3_const_value_init(&arg0);
    v3_const_value_init(&arg1);
    if (strcmp(callee, "len") == 0 && arg_count == 1U) {
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, args[0], &arg0, depth + 1U)) {
            goto fail;
        }
        if (arg0.kind != V3_CONST_STR) {
            goto fail;
        }
        v3_const_value_set_i32(out, (int32_t)strlen(arg0.text_value ? arg0.text_value : ""));
        goto ok;
    }
    if (strcmp(callee, "int32") == 0 && arg_count == 1U) {
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, args[0], &arg0, depth + 1U)) {
            goto fail;
        }
        if (arg0.kind == V3_CONST_I32) {
            v3_const_value_set_i32(out, arg0.i32_value);
            goto ok;
        }
        goto fail;
    }
    if (strcmp(callee, "std/strutils::strip") == 0 && arg_count == 1U) {
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, args[0], &arg0, depth + 1U)) {
            goto fail;
        }
        if (arg0.kind != V3_CONST_STR) {
            goto fail;
        }
        v3_const_str_strip_ascii(arg0.text_value ? arg0.text_value : "", buffer, sizeof(buffer));
        v3_const_value_set_str_kind(out, V3_CONST_STR, buffer);
        goto ok;
    }
    if (strcmp(callee, "std/os::isAbsolute") == 0 && arg_count == 1U) {
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, args[0], &arg0, depth + 1U)) {
            goto fail;
        }
        if (arg0.kind != V3_CONST_STR) {
            goto fail;
        }
        v3_const_value_set_bool(out, v3_const_os_is_absolute(arg0.text_value ? arg0.text_value : ""));
        goto ok;
    }
    if (strcmp(callee, "std/os::joinPath") == 0 && arg_count == 2U) {
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, args[0], &arg0, depth + 1U) ||
            !v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, args[1], &arg1, depth + 1U)) {
            goto fail;
        }
        if (arg0.kind != V3_CONST_STR || arg1.kind != V3_CONST_STR) {
            goto fail;
        }
        v3_const_os_join_path(arg0.text_value ? arg0.text_value : "",
                              arg1.text_value ? arg1.text_value : "",
                              buffer,
                              sizeof(buffer));
        v3_const_value_set_str_kind(out, V3_CONST_STR, buffer);
        goto ok;
    }
fail:
    v3_const_value_free(&arg0);
    v3_const_value_free(&arg1);
    return false;
ok:
    v3_const_value_free(&arg0);
    v3_const_value_free(&arg1);
    return true;
}

static bool v3_const_resolve_call_symbol(const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         const V3LoweredFunctionStub *current_function,
                                         const char *callee,
                                         char *symbol_text,
                                         size_t symbol_cap) {
    char callee_copy[PATH_MAX];
    char *dot;
    snprintf(callee_copy, sizeof(callee_copy), "%s", callee);
    dot = strchr(callee_copy, '.');
    if (dot) {
        const char *module_path;
        *dot = '\0';
        module_path = v3_alias_module_path(aliases, alias_count, callee_copy);
        if (!module_path) {
            return false;
        }
        snprintf(symbol_text, symbol_cap, "%s::%s", module_path, dot + 1);
        return true;
    }
    snprintf(symbol_text, symbol_cap, "%s::%s", current_function->owner_module_path, callee);
    return true;
}

static bool v3_const_eval_expr(const V3LoweringPlanStub *lowering,
                               const V3LoweredFunctionStub *current_function,
                               const V3ImportAlias *aliases,
                               size_t alias_count,
                               V3ConstEnv *env,
                               const char *expr_text,
                               V3ConstValue *out,
                               size_t depth) {
    char expr[8192];
    int32_t op_index;
    if (depth > 64U) {
        return false;
    }
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return false;
    }
    if (expr[0] == '"' && expr[strlen(expr) - 1U] == '"') {
        char literal[8192];
        snprintf(literal, sizeof(literal), "%.*s", (int)(strlen(expr) - 2U), expr + 1);
        return v3_const_value_set_str_kind(out, V3_CONST_STR, literal);
    }
    if (strcmp(expr, "true") == 0) {
        return v3_const_value_set_bool(out, true);
    }
    if (strcmp(expr, "false") == 0) {
        return v3_const_value_set_bool(out, false);
    }
    if ((expr[0] >= '0' && expr[0] <= '9') ||
        (expr[0] == '-' && expr[1] >= '0' && expr[1] <= '9')) {
        return v3_const_value_set_i32(out, (int32_t)strtol(expr, NULL, 10));
    }
    op_index = v3_find_top_level_binary_op(expr, "==");
    if (op_index > 0) {
        V3ConstValue lhs;
        V3ConstValue rhs;
        bool eq = false;
        char left[4096];
        char right[4096];
        v3_const_value_init(&lhs);
        v3_const_value_init(&rhs);
        snprintf(left, sizeof(left), "%.*s", op_index, expr);
        snprintf(right, sizeof(right), "%s", expr + op_index + 2);
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, left, &lhs, depth + 1U) ||
            !v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, right, &rhs, depth + 1U) ||
            !v3_const_value_eq(&lhs, &rhs, &eq)) {
            v3_const_value_free(&lhs);
            v3_const_value_free(&rhs);
            return false;
        }
        v3_const_value_free(&lhs);
        v3_const_value_free(&rhs);
        return v3_const_value_set_bool(out, eq);
    }
    op_index = v3_find_top_level_binary_op(expr, ">");
    if (op_index > 0) {
        V3ConstValue lhs;
        V3ConstValue rhs;
        char left[4096];
        char right[4096];
        v3_const_value_init(&lhs);
        v3_const_value_init(&rhs);
        snprintf(left, sizeof(left), "%.*s", op_index, expr);
        snprintf(right, sizeof(right), "%s", expr + op_index + 1);
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, left, &lhs, depth + 1U) ||
            !v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, right, &rhs, depth + 1U) ||
            lhs.kind != V3_CONST_I32 ||
            rhs.kind != V3_CONST_I32) {
            v3_const_value_free(&lhs);
            v3_const_value_free(&rhs);
            return false;
        }
        v3_const_value_set_bool(out, lhs.i32_value > rhs.i32_value);
        v3_const_value_free(&lhs);
        v3_const_value_free(&rhs);
        return true;
    }
    if (expr[0] == '!') {
        V3ConstValue inner;
        v3_const_value_init(&inner);
        if (!v3_const_eval_expr(lowering, current_function, aliases, alias_count, env, expr + 1, &inner, depth + 1U) ||
            inner.kind != V3_CONST_BOOL) {
            v3_const_value_free(&inner);
            return false;
        }
        v3_const_value_set_bool(out, !inner.bool_value);
        v3_const_value_free(&inner);
        return true;
    }
    if (expr[strlen(expr) - 1U] == ')') {
        char callee[PATH_MAX];
        char (*args)[4096] = NULL;
        size_t arg_count = 0U;
        bool has_named_fields = false;
        size_t i;
        args = (char (*)[4096])v3_xmalloc(sizeof(*args) * 32U);
        if (v3_parse_call_text(expr, callee, sizeof(callee), args, &arg_count, 32U)) {
            for (i = 0; i < arg_count; ++i) {
                if (v3_find_top_level_binary_op(args[i], ":") > 0) {
                    has_named_fields = true;
                    break;
                }
            }
            if (has_named_fields && callee[0] >= 'A' && callee[0] <= 'Z') {
                bool ok = v3_const_record_new_from_named_args(args,
                                                              arg_count,
                                                              lowering,
                                                              current_function,
                                                              aliases,
                                                              alias_count,
                                                              env,
                                                              out,
                                                              depth + 1U);
                free(args);
                return ok;
            }
            if (v3_is_intrinsic_call_name(callee)) {
                bool ok = v3_const_eval_intrinsic_call(callee,
                                                       args,
                                                       arg_count,
                                                       lowering,
                                                       current_function,
                                                       aliases,
                                                       alias_count,
                                                       env,
                                                       out,
                                                       depth + 1U);
                free(args);
                return ok;
            }
            {
                char symbol_text[PATH_MAX];
                int32_t function_index;
                V3ConstValue arg_values[32];
                size_t arg_index;
                char callee_symbol[PATH_MAX];
                bool intrinsic_ok = false;
                for (arg_index = 0; arg_index < arg_count; ++arg_index) {
                    v3_const_value_init(&arg_values[arg_index]);
                }
                if (strchr(callee, '.') != NULL) {
                    char alias_text[128];
                    char function_name[128];
                    char callee_copy[PATH_MAX];
                    char *dot;
                    snprintf(callee_copy, sizeof(callee_copy), "%s", callee);
                    dot = strchr(callee_copy, '.');
                    *dot = '\0';
                    snprintf(alias_text, sizeof(alias_text), "%s", callee_copy);
                    snprintf(function_name, sizeof(function_name), "%s", dot + 1);
                    {
                        const char *module_path = v3_alias_module_path(aliases, alias_count, alias_text);
                        if (module_path) {
                            snprintf(callee_symbol, sizeof(callee_symbol), "%s::%s", module_path, function_name);
                            if (v3_is_intrinsic_module_call(module_path, function_name)) {
                                intrinsic_ok = v3_const_eval_intrinsic_call(callee_symbol,
                                                                            args,
                                                                            arg_count,
                                                                            lowering,
                                                                            current_function,
                                                                            aliases,
                                                                            alias_count,
                                                                            env,
                                                                            out,
                                                                            depth + 1U);
                            }
                        }
                    }
                }
                if (intrinsic_ok) {
                    for (arg_index = 0; arg_index < arg_count; ++arg_index) {
                        v3_const_value_free(&arg_values[arg_index]);
                    }
                    free(args);
                    return true;
                }
                if (!v3_const_resolve_call_symbol(aliases,
                                                  alias_count,
                                                  current_function,
                                                  callee,
                                                  symbol_text,
                                                  sizeof(symbol_text))) {
                    for (arg_index = 0; arg_index < arg_count; ++arg_index) {
                        v3_const_value_free(&arg_values[arg_index]);
                    }
                    free(args);
                    return false;
                }
                function_index = v3_find_lowered_function_index_by_symbol(lowering, symbol_text);
                if (function_index < 0) {
                    for (arg_index = 0; arg_index < arg_count; ++arg_index) {
                        v3_const_value_free(&arg_values[arg_index]);
                    }
                    free(args);
                    return false;
                }
                for (arg_index = 0; arg_index < arg_count; ++arg_index) {
                    if (!v3_const_eval_expr(lowering,
                                            current_function,
                                            aliases,
                                            alias_count,
                                            env,
                                            args[arg_index],
                                            &arg_values[arg_index],
                                            depth + 1U)) {
                        size_t j;
                        for (j = 0; j < arg_count; ++j) {
                            v3_const_value_free(&arg_values[j]);
                        }
                        free(args);
                        return false;
                    }
                }
                if (!v3_const_eval_function(lowering,
                                            &lowering->functions[function_index],
                                            arg_values,
                                            arg_count,
                                            out,
                                            NULL,
                                            depth + 1U)) {
                    size_t j;
                    for (j = 0; j < arg_count; ++j) {
                        v3_const_value_free(&arg_values[j]);
                    }
                    free(args);
                    return false;
                }
                for (arg_index = 0; arg_index < arg_count; ++arg_index) {
                    v3_const_value_free(&arg_values[arg_index]);
                }
                free(args);
                return true;
            }
        }
        free(args);
    }
    if (strchr(expr, '.') != NULL) {
        char chain[16][128];
        size_t chain_len = 0U;
        const char *start = expr;
        const char *cursor = expr;
        V3ConstValue current;
        v3_const_value_init(&current);
        while (1) {
            if (*cursor == '.' || *cursor == '\0') {
                if (chain_len >= 16U) {
                    return false;
                }
                snprintf(chain[chain_len], sizeof(chain[chain_len]), "%.*s", (int)(cursor - start), start);
                v3_trim_copy_text(chain[chain_len], chain[chain_len], sizeof(chain[chain_len]));
                chain_len += 1U;
                if (*cursor == '\0') {
                    break;
                }
                start = cursor + 1;
            }
            cursor++;
        }
        {
            const V3ConstValue *binding = v3_const_env_get(env, chain[0]);
            size_t i;
            if (!binding) {
                v3_const_value_free(&current);
                return false;
            }
            if (!v3_const_value_clone(binding, &current)) {
                return false;
            }
            for (i = 1; i < chain_len; ++i) {
                const V3ConstValue *field = v3_const_record_field(&current, chain[i]);
                V3ConstValue next;
                if (!field) {
                    v3_const_value_free(&current);
                    return false;
                }
                v3_const_value_init(&next);
                if (!v3_const_value_clone(field, &next)) {
                    v3_const_value_free(&current);
                    return false;
                }
                v3_const_value_free(&current);
                current = next;
            }
            if (!v3_const_value_clone(&current, out)) {
                v3_const_value_free(&current);
                return false;
            }
            v3_const_value_free(&current);
            return true;
        }
    }
    {
        const V3ConstValue *binding = v3_const_env_get(env, expr);
        if (binding) {
            return v3_const_value_clone(binding, out);
        }
    }
    return v3_const_value_set_str_kind(out,
                                       (expr[0] >= 'A' && expr[0] <= 'Z') ? V3_CONST_SYMBOL : V3_CONST_STR,
                                       expr);
}

static bool v3_const_parse_binding_name(const char *statement,
                                        const char *prefix,
                                        char *name_out,
                                        size_t name_cap,
                                        char *expr_out,
                                        size_t expr_cap) {
    const char *cursor = statement + strlen(prefix);
    const char *eq = strchr(cursor, '=');
    char name_part[256];
    if (!eq) {
        return false;
    }
    snprintf(name_part, sizeof(name_part), "%.*s", (int)(eq - cursor), cursor);
    v3_trim_copy_text(name_part, name_part, sizeof(name_part));
    if (strchr(name_part, ':') != NULL) {
        *strchr(name_part, ':') = '\0';
    }
    v3_trim_copy_text(name_part, name_out, name_cap);
    snprintf(expr_out, expr_cap, "%s", eq + 1);
    v3_trim_copy_text(expr_out, expr_out, expr_cap);
    return name_out[0] != '\0';
}

static bool v3_const_eval_nested_if_return(const V3LoweringPlanStub *lowering,
                                           const V3LoweredFunctionStub *function,
                                           const V3ImportAlias *aliases,
                                           size_t alias_count,
                                           V3ConstEnv *env,
                                           char **lines,
                                           size_t line_count,
                                           size_t *index_io,
                                           int32_t parent_indent,
                                           V3ConstValue *out,
                                           bool *returned,
                                           size_t depth) {
    while (*index_io < line_count) {
        char statement[8192];
        char *raw = lines[*index_io];
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        snprintf(raw_copy, sizeof(raw_copy), "%s", raw);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            *index_io += 1U;
            continue;
        }
        indent = v3_line_indent(raw);
        if (indent <= parent_indent) {
            return true;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, index_io, statement, sizeof(statement))) {
            return false;
        }
        if (v3_startswith(statement, "return ")) {
            if (!v3_const_eval_expr(lowering,
                                    function,
                                    aliases,
                                    alias_count,
                                    env,
                                    statement + 7,
                                    out,
                                    depth + 1U)) {
                return false;
            }
            *returned = true;
            return true;
        }
        return false;
    }
    return true;
}

static bool v3_const_eval_function(const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *function,
                                   const V3ConstValue *args,
                                   size_t arg_count,
                                   V3ConstValue *out,
                                   V3ConstEntryProgram *entry_program,
                                   size_t depth) {
    char **lines = NULL;
    char *owned_lines = NULL;
    size_t line_count = 0U;
    size_t fn_line_index = 0U;
    char (*param_names)[128] = NULL;
    size_t param_count = 0U;
    char source_path[PATH_MAX];
    V3ImportAlias *aliases = NULL;
    size_t alias_count = 0U;
    V3ConstEnv env;
    size_t i;
    memset(&env, 0, sizeof(env));
    if (depth > 64U) {
        return false;
    }
    param_names = (char (*)[128])v3_xmalloc(sizeof(*param_names) * CHENG_V3_CONST_MAX_BINDINGS);
    aliases = (V3ImportAlias *)v3_xmalloc(sizeof(*aliases) * CHENG_V3_MAX_IMPORT_ALIASES);
    if (!v3_load_function_source_lines(function,
                                       &lines,
                                       &owned_lines,
                                       &line_count,
                                       &fn_line_index,
                                       param_names,
                                       &param_count,
                                       source_path)) {
        free(param_names);
        free(aliases);
        return false;
    }
    v3_parse_import_aliases_from_lines(lines,
                                       line_count,
                                       aliases,
                                       &alias_count,
                                       CHENG_V3_MAX_IMPORT_ALIASES);
    if (param_count != arg_count) {
        free(lines);
        free(owned_lines);
        free(param_names);
        free(aliases);
        return false;
    }
    for (i = 0; i < arg_count; ++i) {
        if (!v3_const_env_set(&env, param_names[i], &args[i])) {
            v3_const_env_free(&env);
            free(lines);
            free(owned_lines);
            free(param_names);
            free(aliases);
            return false;
        }
    }
    for (i = fn_line_index + 1U; i < line_count; ) {
        char statement[8192];
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[i]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            i += 1U;
            continue;
        }
        indent = v3_line_indent(lines[i]);
        if (indent <= 0 && v3_startswith(trimmed, "fn ")) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, &i, statement, sizeof(statement))) {
            v3_const_env_free(&env);
            free(lines);
            free(owned_lines);
            free(param_names);
            free(aliases);
            return false;
        }
        if (v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) {
            char name[128];
            char expr[4096];
            V3ConstValue bound;
            v3_const_value_init(&bound);
            if (!v3_const_parse_binding_name(statement,
                                             v3_startswith(statement, "let ") ? "let " : "var ",
                                             name,
                                             sizeof(name),
                                             expr,
                                             sizeof(expr)) ||
                !v3_const_eval_expr(lowering, function, aliases, alias_count, &env, expr, &bound, depth + 1U) ||
                !v3_const_env_set(&env, name, &bound)) {
                v3_const_value_free(&bound);
                v3_const_env_free(&env);
                free(lines);
                free(owned_lines);
                free(param_names);
                free(aliases);
                return false;
            }
            v3_const_value_free(&bound);
            continue;
        }
        if (v3_startswith(statement, "if ") && statement[strlen(statement) - 1U] == ':') {
            V3ConstValue cond;
            bool returned = false;
            char cond_text[4096];
            size_t nested_index = i;
            v3_const_value_init(&cond);
            snprintf(cond_text, sizeof(cond_text), "%.*s", (int)(strlen(statement) - 4U), statement + 3);
            if (!v3_const_eval_expr(lowering, function, aliases, alias_count, &env, cond_text, &cond, depth + 1U) ||
                cond.kind != V3_CONST_BOOL) {
                v3_const_value_free(&cond);
                v3_const_env_free(&env);
                free(lines);
                free(owned_lines);
                free(param_names);
                free(aliases);
                return false;
            }
            if (cond.bool_value) {
                if (!v3_const_eval_nested_if_return(lowering,
                                                    function,
                                                    aliases,
                                                    alias_count,
                                                    &env,
                                                    lines,
                                                    line_count,
                                                    &nested_index,
                                                    indent,
                                                    out,
                                                    &returned,
                                                    depth + 1U)) {
                    v3_const_value_free(&cond);
                    v3_const_env_free(&env);
                    free(lines);
                    free(owned_lines);
                    free(param_names);
                    free(aliases);
                    return false;
                }
                if (returned) {
                    v3_const_value_free(&cond);
                    v3_const_env_free(&env);
                    free(lines);
                    free(owned_lines);
                    free(param_names);
                    free(aliases);
                    return true;
                }
            } else {
                while (nested_index < line_count) {
                    char nested_copy[4096];
                    char *nested_trimmed;
                    snprintf(nested_copy, sizeof(nested_copy), "%s", lines[nested_index]);
                    nested_trimmed = v3_trim_inplace(nested_copy);
                    if (*nested_trimmed == '\0') {
                        nested_index += 1U;
                        continue;
                    }
                    if (v3_line_indent(lines[nested_index]) <= indent) {
                        break;
                    }
                    nested_index += 1U;
                }
            }
            i = nested_index;
            v3_const_value_free(&cond);
            continue;
        }
        if (v3_startswith(statement, "assert(")) {
            char callee[PATH_MAX];
            char args_text[32][4096];
            size_t statement_arg_count = 0U;
            V3ConstValue cond;
            v3_const_value_init(&cond);
            if (entry_program == NULL ||
                !v3_parse_call_text(statement, callee, sizeof(callee), args_text, &statement_arg_count, 32U) ||
                strcmp(callee, "assert") != 0 ||
                statement_arg_count < 1U ||
                !v3_const_eval_expr(lowering, function, aliases, alias_count, &env, args_text[0], &cond, depth + 1U) ||
                cond.kind != V3_CONST_BOOL ||
                !cond.bool_value) {
                v3_const_value_free(&cond);
                v3_const_env_free(&env);
                free(lines);
                free(owned_lines);
                free(param_names);
                free(aliases);
                return false;
            }
            v3_const_value_free(&cond);
            continue;
        }
        if (v3_startswith(statement, "echo(")) {
            char callee[PATH_MAX];
            char args_text[32][4096];
            size_t statement_arg_count = 0U;
            V3ConstValue echo_value;
            v3_const_value_init(&echo_value);
            if (entry_program == NULL ||
                !v3_parse_call_text(statement, callee, sizeof(callee), args_text, &statement_arg_count, 32U) ||
                strcmp(callee, "echo") != 0 ||
                statement_arg_count != 1U ||
                !v3_const_eval_expr(lowering, function, aliases, alias_count, &env, args_text[0], &echo_value, depth + 1U) ||
                echo_value.kind != V3_CONST_STR ||
                entry_program->echo_count >= CHENG_V3_CONST_MAX_ECHOS) {
                v3_const_value_free(&echo_value);
                v3_const_env_free(&env);
                free(lines);
                free(owned_lines);
                free(param_names);
                free(aliases);
                return false;
            }
            snprintf(entry_program->echo_texts[entry_program->echo_count].text,
                     sizeof(entry_program->echo_texts[entry_program->echo_count].text),
                     "%s",
                     echo_value.text_value ? echo_value.text_value : "");
            entry_program->echo_count += 1U;
            v3_const_value_free(&echo_value);
            continue;
        }
        if (v3_startswith(statement, "return ")) {
            bool ok = v3_const_eval_expr(lowering,
                                         function,
                                         aliases,
                                         alias_count,
                                         &env,
                                         statement + 7,
                                         out,
                                         depth + 1U);
            v3_const_env_free(&env);
            free(lines);
            free(owned_lines);
            free(param_names);
            free(aliases);
            return ok;
        }
        v3_const_env_free(&env);
        free(lines);
        free(owned_lines);
        free(param_names);
        free(aliases);
        return false;
    }
    v3_const_env_free(&env);
    free(lines);
    free(owned_lines);
    free(param_names);
    free(aliases);
    return false;
}

static bool v3_try_consteval_entry_program(const V3SystemLinkPlanStub *plan,
                                           const V3LoweringPlanStub *lowering,
                                           V3ConstEntryProgram *program) {
    const V3LoweredFunctionStub *entry_function = v3_find_entry_lowered_function(lowering);
    V3ConstValue result;
    (void)plan;
    if (!entry_function) {
        return false;
    }
    memset(program, 0, sizeof(*program));
    v3_const_value_init(&result);
    if (!v3_const_eval_function(lowering,
                                entry_function,
                                NULL,
                                0U,
                                &result,
                                program,
                                0U) ||
        result.kind != V3_CONST_I32) {
        v3_const_value_free(&result);
        memset(program, 0, sizeof(*program));
        return false;
    }
    program->ready = true;
    program->return_code = result.i32_value;
    v3_const_value_free(&result);
    return true;
}

static bool v3_parent_dir(const char *path, char *out, size_t cap) {
    const char *slash;
    size_t len;
    if (!path || *path == '\0') {
        out[0] = '\0';
        return false;
    }
    slash = strrchr(path, '/');
    if (!slash) {
        out[0] = '\0';
        return false;
    }
    len = (size_t)(slash - path);
    if (len <= 0U || len >= cap) {
        out[0] = '\0';
        return false;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}

static void v3_join_path(char *out, size_t cap, const char *a, const char *b) {
    if (!a || *a == '\0') {
        v3_copy_text(out, cap, b ? b : "");
        return;
    }
    if (!b || *b == '\0') {
        v3_copy_text(out, cap, a);
        return;
    }
    if (b[0] == '/') {
        v3_copy_text(out, cap, b);
        return;
    }
    if (a[strlen(a) - 1U] == '/') {
        snprintf(out, cap, "%s%s", a, b);
        return;
    }
    snprintf(out, cap, "%s/%s", a, b);
}

static void v3_strip_ext(char *text) {
    char *dot = strrchr(text, '.');
    if (dot) {
        *dot = '\0';
    }
}

static void v3_contract_workspace_root(const V3BootstrapContract *contract, char *out, size_t cap) {
    char bootstrap_dir[PATH_MAX];
    char package_root[PATH_MAX];
    if (!v3_parent_dir(contract->source_path, bootstrap_dir, sizeof(bootstrap_dir)) ||
        !v3_parent_dir(bootstrap_dir, package_root, sizeof(package_root)) ||
        !v3_parent_dir(package_root, out, cap)) {
        out[0] = '\0';
    }
}

static void v3_contract_package_root(const V3BootstrapContract *contract, char *out, size_t cap) {
    char bootstrap_dir[PATH_MAX];
    if (!v3_parent_dir(contract->source_path, bootstrap_dir, sizeof(bootstrap_dir)) ||
        !v3_parent_dir(bootstrap_dir, out, cap)) {
        out[0] = '\0';
    }
}

static void v3_resolve_contract_path(const V3BootstrapContract *contract,
                                     const char *key,
                                     char *out,
                                     size_t cap) {
    char workspace_root[PATH_MAX];
    const char *value = v3_contract_get(contract, key);
    v3_contract_workspace_root(contract, workspace_root, sizeof(workspace_root));
    v3_join_path(out, cap, workspace_root, value ? value : "");
}

static void v3_default_package_root(const V3BootstrapContract *contract, char *out, size_t cap) {
    v3_contract_package_root(contract, out, cap);
}

static void v3_workspace_root_from_package_root(const char *package_root, char *out, size_t cap) {
    const char *slash = strrchr(package_root, '/');
    if (slash && strcmp(slash + 1, "v3") == 0) {
        v3_parent_dir(package_root, out, cap);
        return;
    }
    v3_copy_text(out, cap, package_root);
}

static void v3_package_id_from_root(const char *package_root, char *out, size_t cap) {
    const char *slash = strrchr(package_root, '/');
    const char *base = slash ? slash + 1 : package_root;
    if (strcmp(base, "v3") == 0) {
        v3_copy_text(out, cap, "cheng/v3");
        return;
    }
    snprintf(out, cap, "cheng/%s", base);
}

static bool v3_path_exists_nonempty(const char *path) {
    struct stat st;
    if (!path || *path == '\0') {
        return false;
    }
    if (stat(path, &st) != 0) {
        return false;
    }
    return S_ISREG(st.st_mode) && st.st_size > 0;
}

static bool v3_plan_paths_contains(const V3PlanPath *items, size_t len, const char *path) {
    size_t i;
    for (i = 0; i < len; ++i) {
        if (v3_streq(items[i].text, path)) {
            return true;
        }
    }
    return false;
}

static void v3_plan_add_reason(V3SystemLinkPlanStub *plan, const char *reason) {
    if (plan->missing_reason_count >= CHENG_V3_MAX_PLAN_REASONS) {
        return;
    }
    snprintf(plan->missing_reasons[plan->missing_reason_count],
             sizeof(plan->missing_reasons[plan->missing_reason_count]),
             "%s",
             reason);
    plan->missing_reason_count += 1U;
}

static void v3_lowering_add_reason(V3LoweringPlanStub *plan, const char *reason) {
    if (plan->missing_reason_count >= CHENG_V3_MAX_PLAN_REASONS) {
        return;
    }
    snprintf(plan->missing_reasons[plan->missing_reason_count],
             sizeof(plan->missing_reasons[plan->missing_reason_count]),
             "%s",
             reason);
    plan->missing_reason_count += 1U;
}

static void v3_object_add_reason(V3ObjectPlanStub *plan, const char *reason) {
    if (plan->missing_reason_count >= CHENG_V3_MAX_PLAN_REASONS) {
        return;
    }
    snprintf(plan->missing_reasons[plan->missing_reason_count],
             sizeof(plan->missing_reasons[plan->missing_reason_count]),
             "%s",
             reason);
    plan->missing_reason_count += 1U;
}

static void v3_primary_add_reason(V3PrimaryObjectPlanStub *plan, const char *reason) {
    if (plan->missing_reason_count >= CHENG_V3_MAX_PLAN_REASONS) {
        return;
    }
    snprintf(plan->missing_reasons[plan->missing_reason_count],
             sizeof(plan->missing_reasons[plan->missing_reason_count]),
             "%s",
             reason);
    plan->missing_reason_count += 1U;
}

static void v3_primary_add_unsupported(V3PrimaryObjectPlanStub *plan,
                                       const V3LoweredFunctionStub *function) {
    size_t i;
    char abi_summary[256];
    if (plan->unsupported_function_count >= CHENG_V3_MAX_UNSUPPORTED_DETAILS) {
        return;
    }
    for (i = 0; i < plan->unsupported_function_count; ++i) {
        if (strcmp(plan->unsupported_function_symbols[i].text, function->symbol_text) == 0) {
            return;
        }
    }
    snprintf(plan->unsupported_function_symbols[plan->unsupported_function_count].text,
             sizeof(plan->unsupported_function_symbols[plan->unsupported_function_count].text),
             "%s",
             function->symbol_text);
    snprintf(plan->unsupported_body_kinds[plan->unsupported_function_count],
             sizeof(plan->unsupported_body_kinds[plan->unsupported_function_count]),
             "%s",
             function->body_kind);
    snprintf(plan->unsupported_first_lines[plan->unsupported_function_count],
             sizeof(plan->unsupported_first_lines[plan->unsupported_function_count]),
             "%s",
             function->first_body_line);
    v3_lowered_function_abi_summary(function, abi_summary, sizeof(abi_summary));
    snprintf(plan->unsupported_abi_summaries[plan->unsupported_function_count],
             sizeof(plan->unsupported_abi_summaries[plan->unsupported_function_count]),
             "%s",
             abi_summary);
    plan->unsupported_function_count += 1U;
}

static void v3_native_link_add_reason(V3NativeLinkPlanStub *plan, const char *reason) {
    if (plan->missing_reason_count >= CHENG_V3_MAX_PLAN_REASONS) {
        return;
    }
    snprintf(plan->missing_reasons[plan->missing_reason_count],
             sizeof(plan->missing_reasons[plan->missing_reason_count]),
             "%s",
             reason);
    plan->missing_reason_count += 1U;
}

static bool v3_plan_reasons_contains(char reasons[CHENG_V3_MAX_PLAN_REASONS][128],
                                     size_t reason_count,
                                     const char *reason) {
    size_t i;
    for (i = 0; i < reason_count; ++i) {
        if (strcmp(reasons[i], reason) == 0) {
            return true;
        }
    }
    return false;
}

static void v3_plan_add_path(V3PlanPath *items, size_t *len, size_t cap, const char *path) {
    if (*len >= cap) {
        return;
    }
    snprintf(items[*len].text, sizeof(items[*len].text), "%s", path);
    *len += 1U;
}

static void v3_populate_runtime_requirements(V3SystemLinkPlanStub *plan) {
    if (!v3_streq(plan->emit_kind, "exe")) {
        return;
    }
    if (v3_streq(plan->module_kind, "compiler_control_plane")) {
        v3_plan_add_path(plan->runtime_targets,
                         &plan->runtime_target_count,
                         CHENG_V3_MAX_RUNTIME_TARGETS,
                         "runtime/compiler.tooling_argv_entry");
    } else {
        v3_plan_add_path(plan->runtime_targets,
                         &plan->runtime_target_count,
                         CHENG_V3_MAX_RUNTIME_TARGETS,
                         "runtime/compiler.program_argv_entry");
    }
    v3_plan_add_path(plan->provider_modules,
                     &plan->provider_module_count,
                     CHENG_V3_MAX_PROVIDER_MODULES,
                     "runtime/compiler_runtime_v3");
    v3_plan_add_path(plan->provider_modules,
                     &plan->provider_module_count,
                     CHENG_V3_MAX_PROVIDER_MODULES,
                     "runtime/core_runtime_v3");
    v3_plan_add_path(plan->provider_modules,
                     &plan->provider_module_count,
                     CHENG_V3_MAX_PROVIDER_MODULES,
                     "runtime/program_support_v3");
}

static void v3_source_path_to_module_path(const char *workspace_root,
                                          const char *package_root,
                                          const char *package_id,
                                          const char *source_path,
                                          char *out,
                                          size_t cap) {
    char prefix[PATH_MAX];
    char rel[PATH_MAX];
    char stem[PATH_MAX];
    const char *last_slash;
    snprintf(prefix, sizeof(prefix), "%s/src/", package_root);
    if (v3_startswith(source_path, prefix)) {
        snprintf(rel, sizeof(rel), "%s", source_path + strlen(prefix));
        v3_strip_ext(rel);
        snprintf(out, cap, "%s/%s", package_id, rel);
        return;
    }
    snprintf(prefix, sizeof(prefix), "%s/src/std/", workspace_root);
    if (v3_startswith(source_path, prefix)) {
        snprintf(rel, sizeof(rel), "%s", source_path + strlen(prefix));
        v3_strip_ext(rel);
        snprintf(out, cap, "std/%s", rel);
        return;
    }
    last_slash = strrchr(source_path, '/');
    snprintf(stem, sizeof(stem), "%s", last_slash ? last_slash + 1 : source_path);
    v3_strip_ext(stem);
    v3_copy_text(out, cap, stem);
}

static void v3_module_path_to_source_path(const char *workspace_root,
                                          const char *package_root,
                                          const char *package_id,
                                          const char *module_path,
                                          char *out,
                                          size_t cap) {
    out[0] = '\0';
    if (v3_startswith(module_path, "std/")) {
        char rel[PATH_MAX];
        snprintf(rel, sizeof(rel), "src/%s.cheng", module_path);
        v3_join_path(out, cap, workspace_root, rel);
        return;
    }
    if (v3_streq(module_path, package_id) ||
        (v3_startswith(module_path, package_id) && module_path[strlen(package_id)] == '/')) {
        const char *tail = module_path + strlen(package_id);
        char rel[PATH_MAX];
        if (*tail == '/') {
            tail++;
        }
        snprintf(rel, sizeof(rel), "src/%s.cheng", tail);
        v3_join_path(out, cap, package_root, rel);
        return;
    }
}

static bool v3_source_has_top_level(const char *text) {
    char *owned = v3_strdup(text);
    char *cursor = owned;
    char *line;
    bool out = false;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        char *trimmed = v3_trim_inplace(line);
        if (v3_startswith(trimmed, "import ") ||
            v3_startswith(trimmed, "type") ||
            v3_startswith(trimmed, "fn ") ||
            v3_startswith(trimmed, "iterator ") ||
            v3_startswith(trimmed, "const ") ||
            v3_startswith(trimmed, "let ") ||
            v3_startswith(trimmed, "var ")) {
            out = true;
            break;
        }
    }
    free(owned);
    return out;
}

static bool v3_source_has_main(const char *text) {
    char *owned = v3_strdup(text);
    char *cursor = owned;
    char *line;
    bool out = false;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        char *trimmed = v3_trim_inplace(line);
        if (v3_startswith(trimmed, "fn main(")) {
            out = true;
            break;
        }
    }
    free(owned);
    return out;
}

static bool v3_parse_import_edges(const char *workspace_root,
                                  const char *package_root,
                                  const char *package_id,
                                  const char *owner_module_path,
                                  const char *text,
                                  V3PlanImportEdge *edges,
                                  size_t *edge_len,
                                  size_t edge_cap) {
    char *owned = v3_strdup(text);
    char *cursor = owned;
    char *line;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        char *trimmed = v3_trim_inplace(line);
        char *token;
        if (!v3_startswith(trimmed, "import ")) {
            continue;
        }
        token = trimmed + strlen("import ");
        while (*token != '\0' && isspace((unsigned char)*token)) {
            token++;
        }
        if (*token == '\0') {
            free(owned);
            return false;
        }
        {
            char module_path[PATH_MAX];
            char *stop = token;
            while (*stop != '\0' && !isspace((unsigned char)*stop)) {
                stop++;
            }
            if (*stop != '\0') {
                *stop = '\0';
            }
            snprintf(module_path, sizeof(module_path), "%s", token);
            if (*edge_len >= edge_cap) {
                free(owned);
                return false;
            }
            snprintf(edges[*edge_len].owner_module_path, sizeof(edges[*edge_len].owner_module_path), "%s", owner_module_path);
            snprintf(edges[*edge_len].target_module_path, sizeof(edges[*edge_len].target_module_path), "%s", module_path);
            v3_module_path_to_source_path(workspace_root,
                                          package_root,
                                          package_id,
                                          module_path,
                                          edges[*edge_len].target_source_path,
                                          sizeof(edges[*edge_len].target_source_path));
            edges[*edge_len].resolved = v3_path_exists_nonempty(edges[*edge_len].target_source_path);
            *edge_len += 1U;
        }
    }
    free(owned);
    return true;
}

static bool v3_ident_start_char(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           ch == '_';
}

static bool v3_ident_char(char ch) {
    return v3_ident_start_char(ch) ||
           (ch >= '0' && ch <= '9');
}

static const char *v3_module_default_alias(const char *module_path) {
    const char *slash = strrchr(module_path, '/');
    return slash ? slash + 1 : module_path;
}

static void v3_parse_import_aliases_from_lines(char **lines,
                                               size_t line_count,
                                               V3ImportAlias *aliases,
                                               size_t *alias_count,
                                               size_t alias_cap) {
    size_t i;
    *alias_count = 0U;
    for (i = 0; i < line_count; ++i) {
        char *trimmed = v3_trim_inplace(lines[i]);
        char *cursor;
        char module_path[PATH_MAX];
        char alias_text[128];
        char *space;
        if (!v3_startswith(trimmed, "import ")) {
            continue;
        }
        cursor = trimmed + strlen("import ");
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }
        if (*cursor == '\0') {
            continue;
        }
        if (strchr(cursor, '[') != NULL) {
            continue;
        }
        space = cursor;
        while (*space != '\0' && !isspace((unsigned char)*space)) {
            space++;
        }
        if (space == cursor) {
            continue;
        }
        snprintf(module_path, sizeof(module_path), "%.*s", (int)(space - cursor), cursor);
        snprintf(alias_text, sizeof(alias_text), "%s", v3_module_default_alias(module_path));
        while (*space != '\0' && isspace((unsigned char)*space)) {
            space++;
        }
        if (v3_startswith(space, "as ")) {
            char *alias_cursor = space + 3;
            char *alias_stop = alias_cursor;
            while (*alias_cursor != '\0' && isspace((unsigned char)*alias_cursor)) {
                alias_cursor++;
            }
            alias_stop = alias_cursor;
            while (*alias_stop != '\0' && !isspace((unsigned char)*alias_stop)) {
                alias_stop++;
            }
            if (alias_stop > alias_cursor) {
                snprintf(alias_text, sizeof(alias_text), "%.*s", (int)(alias_stop - alias_cursor), alias_cursor);
            }
        }
        if (*alias_count >= alias_cap) {
            return;
        }
        snprintf(aliases[*alias_count].alias_text, sizeof(aliases[*alias_count].alias_text), "%s", alias_text);
        snprintf(aliases[*alias_count].module_path, sizeof(aliases[*alias_count].module_path), "%s", module_path);
        *alias_count += 1U;
    }
}

static bool v3_load_import_aliases_from_source_path(const char *source_path,
                                                    V3ImportAlias *aliases,
                                                    size_t *alias_count,
                                                    size_t alias_cap) {
    char *owned;
    char *cursor;
    char *line;
    char *lines[CHENG_V3_MAX_PLAN_FUNCTIONS];
    size_t line_count = 0U;
    owned = v3_read_file(source_path);
    if (!owned) {
        return false;
    }
    cursor = owned;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        if (line_count >= CHENG_V3_MAX_PLAN_FUNCTIONS) {
            free(owned);
            return false;
        }
        lines[line_count++] = line;
    }
    v3_parse_import_aliases_from_lines(lines, line_count, aliases, alias_count, alias_cap);
    free(owned);
    return true;
}

static const char *v3_alias_module_path(const V3ImportAlias *aliases,
                                        size_t alias_count,
                                        const char *alias_text) {
    size_t i;
    for (i = 0; i < alias_count; ++i) {
        if (strcmp(aliases[i].alias_text, alias_text) == 0) {
            return aliases[i].module_path;
        }
    }
    return NULL;
}

static bool v3_is_intrinsic_call_name(const char *name) {
    static const char *NAMES[] = {
        "assert",
        "echo",
        "len",
        "IsOk",
        "IsErr",
        "Value",
        "Error",
        "ErrorText",
        "ErrorInfoOf",
        "int",
        "int8",
        "int16",
        "int32",
        "int64",
        "uint8",
        "uint16",
        "uint32",
        "uint64",
        "bool",
        "char",
        "float",
        "float64",
        "str",
        "cstring",
        "ptr"
    };
    size_t i;
    for (i = 0; i < sizeof(NAMES) / sizeof(NAMES[0]); ++i) {
        if (strcmp(NAMES[i], name) == 0) {
            return true;
        }
    }
    return false;
}

static bool v3_is_intrinsic_module_call(const char *module_path, const char *function_name) {
    if (strcmp(module_path, "std/os") == 0 &&
        (strcmp(function_name, "isAbsolute") == 0 ||
         strcmp(function_name, "joinPath") == 0)) {
        return true;
    }
    if (strcmp(module_path, "std/strutils") == 0 &&
        strcmp(function_name, "strip") == 0) {
        return true;
    }
    return false;
}

static bool v3_is_builtin_statement_call_name(const char *callee) {
    if (!callee || *callee == '\0') {
        return false;
    }
    return strcmp(callee, "add") == 0 ||
           strcmp(callee, "assert") == 0 ||
           strcmp(callee, "echo") == 0 ||
           strcmp(callee, "panic") == 0 ||
           strcmp(callee, "system.panic") == 0;
}

static void v3_lowered_function_add_callee(V3LoweredFunctionStub *function,
                                           const char *symbol_text) {
    size_t i;
    if (!symbol_text || *symbol_text == '\0') {
        return;
    }
    for (i = 0; i < function->callee_count; ++i) {
        if (strcmp(function->callee_symbols[i].text, symbol_text) == 0) {
            return;
        }
    }
    if (function->callee_count >= CHENG_V3_MAX_CALLEES) {
        return;
    }
    snprintf(function->callee_symbols[function->callee_count].text,
             sizeof(function->callee_symbols[function->callee_count].text),
             "%s",
             symbol_text);
    function->callee_count += 1U;
}

static void v3_lowered_function_add_all_callees_by_name(const V3LoweringPlanStub *lowering,
                                                        V3LoweredFunctionStub *function,
                                                        const char *function_name) {
    size_t i;
    if (!function_name || *function_name == '\0') {
        return;
    }
    for (i = 0; i < lowering->function_count; ++i) {
        if (strcmp(lowering->functions[i].function_name, function_name) == 0) {
            v3_lowered_function_add_callee(function, lowering->functions[i].symbol_text);
        }
    }
}

static int32_t v3_find_lowered_function_index_by_symbol_scan(const V3LoweringPlanStub *lowering,
                                                             const char *symbol_text) {
    size_t i;
    for (i = 0; i < lowering->function_count; ++i) {
        if (strcmp(lowering->functions[i].symbol_text, symbol_text) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static size_t v3_count_lowered_functions_by_module_and_name(const V3LoweringPlanStub *lowering,
                                                            const char *module_path,
                                                            const char *function_name) {
    size_t i;
    size_t count = 0U;
    for (i = 0; i < lowering->function_count; ++i) {
        if (strcmp(lowering->functions[i].owner_module_path, module_path) == 0 &&
            strcmp(lowering->functions[i].function_name, function_name) == 0) {
            count += 1U;
        }
    }
    return count;
}

static size_t v3_count_lowered_functions_by_name(const V3LoweringPlanStub *lowering,
                                                 const char *function_name) {
    size_t i;
    size_t count = 0U;
    for (i = 0; i < lowering->function_count; ++i) {
        if (strcmp(lowering->functions[i].function_name, function_name) == 0) {
            count += 1U;
        }
    }
    return count;
}

static int32_t v3_find_unique_lowered_function_index_by_name(const V3LoweringPlanStub *lowering,
                                                             const char *function_name) {
    size_t i;
    int32_t found = -1;
    for (i = 0; i < lowering->function_count; ++i) {
        if (strcmp(lowering->functions[i].function_name, function_name) == 0) {
            if (found >= 0) {
                return -2;
            }
            found = (int32_t)i;
        }
    }
    return found;
}

static int32_t v3_find_unique_lowered_function_index_by_module_and_name(const V3LoweringPlanStub *lowering,
                                                                        const char *module_path,
                                                                        const char *function_name) {
    size_t i;
    int32_t found = -1;
    for (i = 0; i < lowering->function_count; ++i) {
        if (strcmp(lowering->functions[i].owner_module_path, module_path) == 0 &&
            strcmp(lowering->functions[i].function_name, function_name) == 0) {
            if (found >= 0) {
                return -2;
            }
            found = (int32_t)i;
        }
    }
    return found;
}

static bool v3_resolve_bare_call_symbol(const V3LoweringPlanStub *lowering,
                                        const char *owner_module_path,
                                        const V3ImportAlias *aliases,
                                        size_t alias_count,
                                        const char *function_name,
                                        char *out,
                                        size_t cap) {
    size_t i;
    size_t match_count = 0U;
    char candidate[PATH_MAX];
    if (v3_count_lowered_functions_by_module_and_name(lowering, owner_module_path, function_name) == 1U) {
        snprintf(candidate, sizeof(candidate), "%s::%s", owner_module_path, function_name);
        snprintf(out, cap, "%s", candidate);
        return true;
    }
    out[0] = '\0';
    for (i = 0; i < alias_count; ++i) {
        size_t local_matches =
            v3_count_lowered_functions_by_module_and_name(lowering,
                                                          aliases[i].module_path,
                                                          function_name);
        if (local_matches > 1U) {
            out[0] = '\0';
            return false;
        }
        snprintf(candidate,
                 sizeof(candidate),
                 "%s::%s",
                 aliases[i].module_path,
                 function_name);
        if (local_matches == 1U) {
            match_count += 1U;
            snprintf(out, cap, "%s", candidate);
            if (match_count > 1U) {
                out[0] = '\0';
                return false;
            }
        }
    }
    if (match_count == 1U) {
        return true;
    }
    if (v3_count_lowered_functions_by_name(lowering, function_name) == 1U) {
        int32_t global_index = v3_find_unique_lowered_function_index_by_name(lowering, function_name);
        if (global_index >= 0) {
            snprintf(out, cap, "%s", lowering->functions[global_index].symbol_text);
            return true;
        }
    }
    return false;
}

static void v3_strip_trailing_type_args_text(const char *text, char *out, size_t cap) {
    char trimmed[PATH_MAX];
    int32_t depth = 0;
    int32_t open_index = -1;
    size_t i;
    size_t len;
    v3_trim_copy_text(text, trimmed, sizeof(trimmed));
    len = strlen(trimmed);
    for (i = 0; i < len; ++i) {
        if (trimmed[i] == '[') {
            if (depth == 0) {
                open_index = (int32_t)i;
            }
            depth += 1;
            continue;
        }
        if (trimmed[i] == ']') {
            if (depth > 0) {
                depth -= 1;
            }
            continue;
        }
    }
    if (open_index > 0 && len > 0U && trimmed[len - 1U] == ']' && depth == 0) {
        snprintf(out, cap, "%.*s", open_index, trimmed);
        v3_trim_copy_text(out, out, cap);
        return;
    }
    v3_copy_text(out, cap, trimmed);
}

static bool v3_extract_trailing_type_arg_text(const char *text, char *out, size_t cap) {
    char trimmed[PATH_MAX];
    int32_t depth = 0;
    int32_t open_index = -1;
    size_t i;
    size_t len;
    v3_trim_copy_text(text, trimmed, sizeof(trimmed));
    len = strlen(trimmed);
    for (i = 0; i < len; ++i) {
        if (trimmed[i] == '[') {
            if (depth == 0) {
                open_index = (int32_t)i;
            }
            depth += 1;
            continue;
        }
        if (trimmed[i] == ']') {
            if (depth == 0) {
                return false;
            }
            depth -= 1;
            continue;
        }
    }
    if (open_index <= 0 || len <= (size_t)open_index + 1U || trimmed[len - 1U] != ']' || depth != 0) {
        return false;
    }
    snprintf(out, cap, "%.*s", (int)(len - (size_t)open_index - 2U), trimmed + open_index + 1);
    v3_trim_copy_text(out, out, cap);
    return out[0] != '\0';
}

static V3ResultIntrinsicKind v3_result_intrinsic_kind_from_name(const char *function_name) {
    if (strcmp(function_name, "IsOk") == 0) {
        return V3_RESULT_INTRINSIC_IS_OK;
    }
    if (strcmp(function_name, "IsErr") == 0) {
        return V3_RESULT_INTRINSIC_IS_ERR;
    }
    if (strcmp(function_name, "Value") == 0) {
        return V3_RESULT_INTRINSIC_VALUE;
    }
    if (strcmp(function_name, "Error") == 0) {
        return V3_RESULT_INTRINSIC_ERROR;
    }
    if (strcmp(function_name, "ErrorText") == 0) {
        return V3_RESULT_INTRINSIC_ERROR_TEXT;
    }
    if (strcmp(function_name, "ErrorInfoOf") == 0) {
        return V3_RESULT_INTRINSIC_ERROR_INFO_OF;
    }
    if (strcmp(function_name, "Ok") == 0) {
        return V3_RESULT_INTRINSIC_OK;
    }
    if (strcmp(function_name, "Err") == 0) {
        return V3_RESULT_INTRINSIC_ERR;
    }
    if (strcmp(function_name, "ErrCode") == 0) {
        return V3_RESULT_INTRINSIC_ERR_CODE;
    }
    if (strcmp(function_name, "ErrInfo") == 0) {
        return V3_RESULT_INTRINSIC_ERR_INFO;
    }
    return V3_RESULT_INTRINSIC_NONE;
}

static V3ResultIntrinsicKind v3_resolve_result_intrinsic_kind(const V3LoweringPlanStub *lowering,
                                                              const char *owner_module_path,
                                                              const V3ImportAlias *aliases,
                                                              size_t alias_count,
                                                              const char *callee) {
    char stripped[PATH_MAX];
    char resolved_symbol[PATH_MAX];
    char module_or_alias[PATH_MAX];
    char function_name[128];
    char direct[PATH_MAX];
    char *dot;
    char *scope;
    V3ResultIntrinsicKind kind;
    v3_strip_trailing_type_args_text(callee, stripped, sizeof(stripped));
    if (stripped[0] == '\0') {
        return V3_RESULT_INTRINSIC_NONE;
    }
    snprintf(direct, sizeof(direct), "%s", stripped);
    scope = strrchr(direct, ':');
    if (scope && scope != direct && scope[-1] == ':') {
        scope[-1] = '\0';
        kind = v3_result_intrinsic_kind_from_name(scope + 1);
        if (kind != V3_RESULT_INTRINSIC_NONE && strcmp(direct, "std/result") == 0) {
            return kind;
        }
        return V3_RESULT_INTRINSIC_NONE;
    }
    snprintf(module_or_alias, sizeof(module_or_alias), "%s", stripped);
    dot = strchr(module_or_alias, '.');
    if (dot) {
        const char *module_path;
        *dot = '\0';
        snprintf(function_name, sizeof(function_name), "%s", dot + 1);
        kind = v3_result_intrinsic_kind_from_name(function_name);
        if (kind == V3_RESULT_INTRINSIC_NONE) {
            return V3_RESULT_INTRINSIC_NONE;
        }
        module_path = v3_alias_module_path(aliases, alias_count, module_or_alias);
        if (module_path && strcmp(module_path, "std/result") == 0) {
            return kind;
        }
        return V3_RESULT_INTRINSIC_NONE;
    }
    kind = v3_result_intrinsic_kind_from_name(stripped);
    if (kind == V3_RESULT_INTRINSIC_NONE) {
        return V3_RESULT_INTRINSIC_NONE;
    }
    (void)lowering;
    (void)owner_module_path;
    (void)aliases;
    (void)alias_count;
    resolved_symbol[0] = '\0';
    return kind;
}

static void v3_collect_calls_from_text_resolved(const char *expr_text,
                                                const char *owner_module_path,
                                                const V3ImportAlias *aliases,
                                                size_t alias_count,
                                                const V3LoweringPlanStub *lowering,
                                                V3LoweredFunctionStub *function) {
    const char *cursor = expr_text;
    bool in_string = false;
    while (*cursor != '\0') {
        if (*cursor == '"') {
            in_string = !in_string;
            cursor++;
            continue;
        }
        if (in_string) {
            cursor++;
            continue;
        }
        if (v3_ident_start_char(*cursor)) {
            const char *start = cursor;
            const char *stop = cursor + 1;
            char token[PATH_MAX];
            char resolved_symbol[PATH_MAX];
            const char *lookahead;
            while (*stop != '\0') {
                if (*stop == '.') {
                    if (!v3_ident_start_char(stop[1])) {
                        break;
                    }
                    stop++;
                    while (v3_ident_char(*stop)) {
                        stop++;
                    }
                    continue;
                }
                if (!v3_ident_char(*stop)) {
                    break;
                }
                stop++;
            }
            snprintf(token, sizeof(token), "%.*s", (int)(stop - start), start);
            lookahead = stop;
            while (*lookahead != '\0' && isspace((unsigned char)*lookahead)) {
                lookahead++;
            }
            if (*lookahead == '(') {
                if (v3_is_builtin_statement_call_name(token)) {
                    cursor = stop;
                    continue;
                }
                char *dot = strchr(token, '.');
                V3ResultIntrinsicKind result_kind =
                    v3_resolve_result_intrinsic_kind(lowering,
                                                     owner_module_path,
                                                     aliases,
                                                     alias_count,
                                                     token);
                if (dot) {
                    char alias_text[128];
                    char function_name[128];
                    const char *module_path;
                    snprintf(alias_text, sizeof(alias_text), "%.*s", (int)(dot - token), token);
                    snprintf(function_name, sizeof(function_name), "%s", dot + 1);
                    module_path = v3_alias_module_path(aliases, alias_count, alias_text);
                    if (module_path &&
                        !v3_is_intrinsic_module_call(module_path, function_name) &&
                        v3_result_intrinsic_kind_from_name(function_name) == V3_RESULT_INTRINSIC_NONE &&
                        v3_count_lowered_functions_by_module_and_name(lowering, module_path, function_name) == 1U) {
                        snprintf(resolved_symbol, sizeof(resolved_symbol), "%s::%s", module_path, function_name);
                        if (v3_find_lowered_function_index_by_symbol_scan(lowering, resolved_symbol) >= 0) {
                            v3_lowered_function_add_callee(function, resolved_symbol);
                        }
                    }
                } else if (!v3_is_intrinsic_call_name(token) &&
                           result_kind == V3_RESULT_INTRINSIC_NONE &&
                           v3_resolve_bare_call_symbol(lowering,
                                                       owner_module_path,
                                                       aliases,
                                                       alias_count,
                                                       token,
                                                       resolved_symbol,
                                                       sizeof(resolved_symbol))) {
                    v3_lowered_function_add_callee(function, resolved_symbol);
                } else if (!v3_is_intrinsic_call_name(token) &&
                           result_kind == V3_RESULT_INTRINSIC_NONE) {
                    v3_lowered_function_add_all_callees_by_name(lowering, function, token);
                }
            } else if (*lookahead != '\0' &&
                       (v3_ident_start_char(*lookahead) ||
                        isdigit((unsigned char)*lookahead) ||
                        *lookahead == '"' ||
                        *lookahead == '-' ||
                        *lookahead == '!' ||
                        *lookahead == '[')) {
                if (v3_is_builtin_statement_call_name(token)) {
                    cursor = stop;
                    continue;
                }
                char *dot = strchr(token, '.');
                V3ResultIntrinsicKind result_kind =
                    v3_resolve_result_intrinsic_kind(lowering,
                                                     owner_module_path,
                                                     aliases,
                                                     alias_count,
                                                     token);
                if (dot) {
                    char alias_text[128];
                    char function_name[128];
                    const char *module_path;
                    snprintf(alias_text, sizeof(alias_text), "%.*s", (int)(dot - token), token);
                    snprintf(function_name, sizeof(function_name), "%s", dot + 1);
                    module_path = v3_alias_module_path(aliases, alias_count, alias_text);
                    if (module_path &&
                        !v3_is_intrinsic_module_call(module_path, function_name) &&
                        v3_result_intrinsic_kind_from_name(function_name) == V3_RESULT_INTRINSIC_NONE &&
                        v3_count_lowered_functions_by_module_and_name(lowering, module_path, function_name) == 1U) {
                        snprintf(resolved_symbol, sizeof(resolved_symbol), "%s::%s", module_path, function_name);
                        if (v3_find_lowered_function_index_by_symbol_scan(lowering, resolved_symbol) >= 0) {
                            v3_lowered_function_add_callee(function, resolved_symbol);
                        }
                    }
                } else if (!v3_is_intrinsic_call_name(token) &&
                           result_kind == V3_RESULT_INTRINSIC_NONE &&
                           v3_resolve_bare_call_symbol(lowering,
                                                       owner_module_path,
                                                       aliases,
                                                       alias_count,
                                                       token,
                                                       resolved_symbol,
                                                       sizeof(resolved_symbol))) {
                    v3_lowered_function_add_callee(function, resolved_symbol);
                } else if (!v3_is_intrinsic_call_name(token) &&
                           result_kind == V3_RESULT_INTRINSIC_NONE) {
                    v3_lowered_function_add_all_callees_by_name(lowering, function, token);
                }
            }
            cursor = stop;
            continue;
        }
        cursor++;
    }
}

static bool v3_lowering_function_name_from_line(char *trimmed, char *out, size_t cap) {
    char *token;
    char *stop;
    out[0] = '\0';
    if (!v3_startswith(trimmed, "fn ")) {
        return false;
    }
    token = trimmed + 3;
    while (*token != '\0' && isspace((unsigned char)*token)) {
        token++;
    }
    if (*token == '\0') {
        return false;
    }
    stop = token;
    while (*stop != '\0' &&
           *stop != '(' &&
           *stop != ' ' &&
           *stop != '\t') {
        stop++;
    }
    if (stop == token) {
        return false;
    }
    *stop = '\0';
    snprintf(out, cap, "%s", token);
    return true;
}

static bool v3_lowering_parse_return_call_noarg_i32(const char *trimmed,
                                                    char *target_name,
                                                    size_t cap) {
    const char *prefix = "return ";
    const char *call;
    const char *open_paren;
    size_t name_len;
    if (!v3_startswith(trimmed, prefix)) {
        return false;
    }
    call = trimmed + strlen(prefix);
    open_paren = strchr(call, '(');
    if (!open_paren || strcmp(open_paren, "()") != 0) {
        return false;
    }
    name_len = (size_t)(open_paren - call);
    if (name_len <= 0U || name_len + 1U > cap) {
        return false;
    }
    memcpy(target_name, call, name_len);
    target_name[name_len] = '\0';
    return true;
}

static size_t v3_function_body_last_line_number(char **lines,
                                                size_t line_count,
                                                size_t signature_end_index) {
    size_t i;
    size_t out = 0U;
    for (i = signature_end_index + 1U; i < line_count; ++i) {
        char *trimmed = v3_trim_inplace(lines[i]);
        if (*trimmed == '\0') {
            continue;
        }
        if (v3_startswith(trimmed, "fn ")) {
            break;
        }
        out = i + 1U;
    }
    return out;
}

static void v3_lowering_function_body_kind(char **lines,
                                           size_t line_count,
                                           size_t signature_end_index,
                                           char *body_kind,
                                           size_t body_kind_cap,
                                           char *call_target,
                                           size_t call_target_cap,
                                           char *first_body_line,
                                           size_t first_body_line_cap,
                                           size_t *body_first_line_number_out,
                                           size_t *body_last_line_number_out) {
    size_t i;
    bool saw_body_line = false;
    snprintf(body_kind, body_kind_cap, "%s", "unsupported");
    call_target[0] = '\0';
    first_body_line[0] = '\0';
    if (body_first_line_number_out != NULL) {
        *body_first_line_number_out = 0U;
    }
    if (body_last_line_number_out != NULL) {
        *body_last_line_number_out = 0U;
    }
    for (i = signature_end_index + 1U; i < line_count; ++i) {
        char call_name[128];
        char *trimmed = v3_trim_inplace(lines[i]);
        if (*trimmed == '\0') {
            continue;
        }
        if (v3_startswith(trimmed, "fn ")) {
            break;
        }
        if (body_first_line_number_out != NULL && *body_first_line_number_out == 0U) {
            *body_first_line_number_out = i + 1U;
        }
        if (body_last_line_number_out != NULL) {
            *body_last_line_number_out = i + 1U;
        }
        if (saw_body_line) {
            snprintf(body_kind, body_kind_cap, "%s", "unsupported");
            call_target[0] = '\0';
            return;
        }
        saw_body_line = true;
        snprintf(first_body_line, first_body_line_cap, "%s", trimmed);
        if (strcmp(trimmed, "return 0") == 0) {
            snprintf(body_kind, body_kind_cap, "%s", "return_zero_i32");
            return;
        }
        if (v3_lowering_parse_return_call_noarg_i32(trimmed, call_name, sizeof(call_name))) {
            snprintf(body_kind, body_kind_cap, "%s", "return_call_noarg_i32");
            snprintf(call_target, call_target_cap, "%s", call_name);
            return;
        }
        if (v3_startswith(trimmed, "return ")) {
            snprintf(body_kind, body_kind_cap, "%s", "return_expr");
            return;
        }
        if (v3_startswith(trimmed, "var ")) {
            snprintf(body_kind, body_kind_cap, "%s", "stmt_var");
            return;
        }
        if (v3_startswith(trimmed, "let ")) {
            snprintf(body_kind, body_kind_cap, "%s", "stmt_let");
            return;
        }
        if (v3_startswith(trimmed, "if ")) {
            snprintf(body_kind, body_kind_cap, "%s", "stmt_if");
            return;
        }
        if (v3_startswith(trimmed, "for ")) {
            snprintf(body_kind, body_kind_cap, "%s", "stmt_for");
            return;
        }
        if (v3_startswith(trimmed, "while ")) {
            snprintf(body_kind, body_kind_cap, "%s", "stmt_while");
            return;
        }
        if (v3_startswith(trimmed, "case ")) {
            snprintf(body_kind, body_kind_cap, "%s", "stmt_case");
            return;
        }
        if (strchr(trimmed, '(') != NULL && trimmed[strlen(trimmed) - 1U] == ')') {
            snprintf(body_kind, body_kind_cap, "%s", "stmt_call");
            return;
        }
        snprintf(body_kind, body_kind_cap, "%s", "unsupported");
        call_target[0] = '\0';
        return;
    }
}

static bool v3_collect_lowering_functions_from_source(const V3SystemLinkPlanStub *plan,
                                                      const char *source_path,
                                                      V3LoweringPlanStub *lowering) {
    char owner_module_path[PATH_MAX];
    V3ImportAlias aliases[CHENG_V3_MAX_IMPORT_ALIASES];
    size_t alias_count = 0U;
    char *owned;
    char *cursor;
    char *line;
    char *lines[CHENG_V3_MAX_PLAN_FUNCTIONS];
    size_t line_count = 0U;
    size_t i;
    v3_source_path_to_module_path(plan->workspace_root,
                                  plan->package_root,
                                  plan->package_id,
                                  source_path,
                                  owner_module_path,
                                  sizeof(owner_module_path));
    owned = v3_read_file(source_path);
    if (!owned) {
        return false;
    }
    cursor = owned;
    while ((line = strsep(&cursor, "\n")) != NULL) {
        if (line_count >= CHENG_V3_MAX_PLAN_FUNCTIONS) {
            free(owned);
            fprintf(stderr, "[cheng_v3_seed] lowering source line inventory too large\n");
            return false;
        }
        lines[line_count++] = line;
    }
    v3_parse_import_aliases_from_lines(lines,
                                       line_count,
                                       aliases,
                                       &alias_count,
                                       CHENG_V3_MAX_IMPORT_ALIASES);
    for (i = 0; i < line_count; ++i) {
        char function_name[128];
        char signature_text[8192];
        char line_copy[4096];
        size_t signature_end_index = 0U;
        size_t body_statement_count = 0U;
        char *trimmed;
        snprintf(line_copy, sizeof(line_copy), "%s", lines[i]);
        trimmed = v3_trim_inplace(line_copy);
        if (!v3_lowering_function_name_from_line(trimmed, function_name, sizeof(function_name))) {
            continue;
        }
        if (!v3_collect_function_signature(lines,
                                           line_count,
                                           i,
                                           signature_text,
                                           sizeof(signature_text),
                                           &signature_end_index)) {
            continue;
        }
        if (lowering->function_count >= CHENG_V3_MAX_PLAN_FUNCTIONS) {
            free(owned);
            fprintf(stderr, "[cheng_v3_seed] lowering function inventory too large\n");
            return false;
        }
        snprintf(lowering->functions[lowering->function_count].source_path,
                 sizeof(lowering->functions[lowering->function_count].source_path),
                 "%s",
                 source_path);
        snprintf(lowering->functions[lowering->function_count].owner_module_path,
                 sizeof(lowering->functions[lowering->function_count].owner_module_path),
                 "%s",
                 owner_module_path);
        snprintf(lowering->functions[lowering->function_count].function_name,
                 sizeof(lowering->functions[lowering->function_count].function_name),
                 "%s",
                 function_name);
        snprintf(lowering->functions[lowering->function_count].symbol_text,
                 sizeof(lowering->functions[lowering->function_count].symbol_text),
                 "%s::%s",
                 owner_module_path,
                 function_name);
        lowering->functions[lowering->function_count].signature_line_number = i + 1U;
        if (!v3_parse_function_signature_meta(signature_text,
                                              function_name,
                                              &lowering->functions[lowering->function_count])) {
            free(owned);
            fprintf(stderr, "[cheng_v3_seed] malformed function signature metadata\n");
            return false;
        }
        v3_lowering_function_body_kind(lines,
                                       line_count,
                                       signature_end_index,
                                       lowering->functions[lowering->function_count].body_kind,
                                       sizeof(lowering->functions[lowering->function_count].body_kind),
                                       lowering->functions[lowering->function_count].body_call_target,
                                       sizeof(lowering->functions[lowering->function_count].body_call_target),
                                       lowering->functions[lowering->function_count].first_body_line,
                                       sizeof(lowering->functions[lowering->function_count].first_body_line),
                                       &lowering->functions[lowering->function_count].body_first_line_number,
                                       &lowering->functions[lowering->function_count].body_last_line_number);
        lowering->functions[lowering->function_count].body_last_line_number =
            v3_function_body_last_line_number(lines, line_count, signature_end_index);
        body_statement_count =
            v3_count_function_body_statements(lines, line_count, signature_end_index);
        if (strcmp(lowering->functions[lowering->function_count].return_abi_class, "void") != 0) {
            if (body_statement_count == 1U &&
                strcmp(lowering->functions[lowering->function_count].body_kind, "stmt_call") == 0) {
                snprintf(lowering->functions[lowering->function_count].body_kind,
                         sizeof(lowering->functions[lowering->function_count].body_kind),
                         "%s",
                         "return_expr");
            } else if (body_statement_count == 1U &&
                       strcmp(lowering->functions[lowering->function_count].body_kind, "unsupported") == 0 &&
                       lowering->functions[lowering->function_count].first_body_line[0] != '\0' &&
                       !v3_startswith(lowering->functions[lowering->function_count].first_body_line, "if ") &&
                       !v3_startswith(lowering->functions[lowering->function_count].first_body_line, "for ") &&
                       !v3_startswith(lowering->functions[lowering->function_count].first_body_line, "while ") &&
                       !v3_startswith(lowering->functions[lowering->function_count].first_body_line, "case ") &&
                       !v3_startswith(lowering->functions[lowering->function_count].first_body_line, "of ")) {
                snprintf(lowering->functions[lowering->function_count].body_kind,
                         sizeof(lowering->functions[lowering->function_count].body_kind),
                         "%s",
                         "return_expr");
            }
        }
        lowering->functions[lowering->function_count].is_entry =
            v3_streq(source_path, plan->entry_path) &&
            v3_streq(function_name, plan->entry_symbol);
        lowering->functions[lowering->function_count].reachable = false;
        lowering->functions[lowering->function_count].callee_count = 0U;
        lowering->function_count += 1U;
    }
    free(owned);
    return true;
}

static int32_t v3_find_lowered_function_index_by_symbol(const V3LoweringPlanStub *lowering,
                                                        const char *symbol_text) {
    return v3_find_lowered_function_index_by_symbol_scan(lowering, symbol_text);
}

static bool v3_refresh_function_callees(const V3LoweringPlanStub *lowering,
                                        V3LoweredFunctionStub *function) {
    char **lines = NULL;
    char *owned_lines = NULL;
    size_t line_count = 0U;
    size_t fn_line_index = 0U;
    char param_names[CHENG_V3_CONST_MAX_BINDINGS][128];
    size_t param_count = 0U;
    char owned_path[PATH_MAX];
    V3ImportAlias aliases[CHENG_V3_MAX_IMPORT_ALIASES];
    size_t alias_count = 0U;
    size_t body_line_index;
    if (!v3_load_function_source_lines(function,
                                       &lines,
                                       &owned_lines,
                                       &line_count,
                                       &fn_line_index,
                                       param_names,
                                       &param_count,
                                       owned_path)) {
        return false;
    }
    (void)param_names;
    (void)param_count;
    v3_parse_import_aliases_from_lines(lines,
                                       line_count,
                                       aliases,
                                       &alias_count,
                                       CHENG_V3_MAX_IMPORT_ALIASES);
    function->callee_count = 0U;
    for (body_line_index = fn_line_index + 1U; body_line_index < line_count; ++body_line_index) {
        char *body_trimmed = v3_trim_inplace(lines[body_line_index]);
        if (*body_trimmed == '\0') {
            continue;
        }
        if (v3_startswith(body_trimmed, "fn ")) {
            break;
        }
        v3_collect_calls_from_text_resolved(body_trimmed,
                                            function->owner_module_path,
                                            aliases,
                                            alias_count,
                                            lowering,
                                            function);
    }
    free(lines);
    free(owned_lines);
    return true;
}

static bool v3_refresh_lowering_callees(V3LoweringPlanStub *lowering) {
    size_t i;
    for (i = 0; i < lowering->function_count; ++i) {
        if (!v3_refresh_function_callees(lowering, &lowering->functions[i])) {
            return false;
        }
    }
    return true;
}

static void v3_prune_lowering_to_reachable(V3LoweringPlanStub *lowering) {
    int32_t queue[CHENG_V3_MAX_PLAN_FUNCTIONS];
    size_t queue_head = 0U;
    size_t queue_tail = 0U;
    size_t i;
    size_t write_index = 0U;
    bool saw_entry = false;
    for (i = 0; i < lowering->function_count; ++i) {
        lowering->functions[i].reachable = false;
        if (lowering->functions[i].is_entry) {
            lowering->functions[i].reachable = true;
            queue[queue_tail++] = (int32_t)i;
            saw_entry = true;
        }
    }
    if (!saw_entry) {
        return;
    }
    while (queue_head < queue_tail) {
        const V3LoweredFunctionStub *function = &lowering->functions[queue[queue_head++]];
        size_t callee_index;
        for (callee_index = 0; callee_index < function->callee_count; ++callee_index) {
            int32_t found = v3_find_lowered_function_index_by_symbol(lowering,
                                                                     function->callee_symbols[callee_index].text);
            if (found >= 0 && !lowering->functions[found].reachable) {
                lowering->functions[found].reachable = true;
                queue[queue_tail++] = found;
            }
        }
    }
    for (i = 0; i < lowering->function_count; ++i) {
        if (!lowering->functions[i].reachable) {
            continue;
        }
        if (write_index != i) {
            lowering->functions[write_index] = lowering->functions[i];
        }
        write_index += 1U;
    }
    lowering->function_count = write_index;
}

static bool v3_build_lowering_plan_stub(const V3SystemLinkPlanStub *plan,
                                        V3LoweringPlanStub *lowering) {
    size_t i;
    bool saw_entry = false;
    memset(lowering, 0, sizeof(*lowering));
    for (i = 0; i < plan->source_closure_count; ++i) {
        if (!v3_collect_top_level_consts_from_source(plan,
                                                     plan->source_closure_paths[i].text,
                                                     lowering)) {
            return false;
        }
    }
    for (i = 0; i < plan->source_closure_count; ++i) {
        if (!v3_collect_type_defs_from_source(plan,
                                              plan->source_closure_paths[i].text,
                                              lowering)) {
            return false;
        }
    }
    for (i = 0; i < plan->source_closure_count; ++i) {
        if (!v3_collect_lowering_functions_from_source(plan,
                                                       plan->source_closure_paths[i].text,
                                                       lowering)) {
            return false;
        }
    }
    if (lowering->function_count <= 0U) {
        v3_lowering_add_reason(lowering, "lowering_functions_missing");
        return true;
    }
    if (plan->entry_symbol[0] != '\0') {
        for (i = 0; i < lowering->function_count; ++i) {
            if (lowering->functions[i].is_entry) {
                saw_entry = true;
                break;
            }
        }
        if (!saw_entry) {
            v3_lowering_add_reason(lowering, "lowering_entry_function_missing");
        }
    }
    if (!v3_refresh_lowering_callees(lowering)) {
        v3_lowering_add_reason(lowering, "lowering_callee_refresh_failed");
        return true;
    }
    v3_prune_lowering_to_reachable(lowering);
    return true;
}

static void v3_sanitize_path_part(const char *text, char *out, size_t cap) {
    size_t i;
    size_t used = 0U;
    if (!text || cap <= 0U) {
        return;
    }
    out[0] = '\0';
    for (i = 0; text[i] != '\0' && used + 1U < cap; ++i) {
        const char ch = text[i];
        const bool digit = ch >= '0' && ch <= '9';
        const bool upper = ch >= 'A' && ch <= 'Z';
        const bool lower = ch >= 'a' && ch <= 'z';
        out[used++] = (digit || upper || lower) ? ch : '_';
    }
    out[used] = '\0';
}

static void v3_primary_symbol_name(const V3SystemLinkPlanStub *plan,
                                   const V3LoweredFunctionStub *function,
                                   char *out,
                                   size_t cap) {
    char raw[PATH_MAX];
    (void)plan;
    snprintf(raw, sizeof(raw), "%s__%s", function->owner_module_path, function->function_name);
    if (cap <= 1U) {
        return;
    }
    out[0] = '_';
    v3_sanitize_path_part(raw, out + 1, cap - 1U);
}

static void v3_runtime_entry_bridge_symbol(const V3SystemLinkPlanStub *plan,
                                           char *out,
                                           size_t cap) {
    snprintf(out,
             cap,
             "%s",
             v3_streq(plan->module_kind, "compiler_control_plane") ?
                 "_cheng_v3_tooling_argv_entry" :
                 "_cheng_v3_program_argv_entry");
}

static void v3_line_map_path_for_output(const char *output_path,
                                        char *out,
                                        size_t cap) {
    snprintf(out, cap, "%s.v3.map", output_path);
}

static bool v3_write_executable_line_map(const V3SystemLinkPlanStub *plan,
                                         const V3LoweringPlanStub *lowering) {
    const V3LoweredFunctionStub *entry_function;
    char map_path[PATH_MAX];
    size_t cap;
    char *text;
    size_t i;
    if (!v3_streq(plan->emit_kind, "exe") || plan->output_path[0] == '\0') {
        return true;
    }
    v3_line_map_path_for_output(plan->output_path, map_path, sizeof(map_path));
    entry_function = v3_find_entry_lowered_function(lowering);
    cap = 256U + (lowering->function_count + 1U) * (PATH_MAX * 2U + 192U);
    text = (char *)v3_xmalloc(cap);
    text[0] = '\0';
    strncat(text, "v3_line_map_v1\n", cap - strlen(text) - 1U);
    snprintf(text + strlen(text),
             cap - strlen(text),
             "entry_count=%zu\n",
             lowering->function_count + (entry_function != NULL ? 1U : 0U));
    for (i = 0; i < lowering->function_count; ++i) {
        char primary_symbol[PATH_MAX];
        const V3LoweredFunctionStub *function = &lowering->functions[i];
        v3_primary_symbol_name(plan, function, primary_symbol, sizeof(primary_symbol));
        snprintf(text + strlen(text),
                 cap - strlen(text),
                 "entry\t%s\t%s\t%s\t%zu\t%zu\t%zu\n",
                 primary_symbol,
                 function->function_name,
                 function->source_path,
                 function->signature_line_number,
                 function->body_first_line_number,
                 function->body_last_line_number);
    }
    if (entry_function != NULL) {
        char bridge_symbol[PATH_MAX];
        v3_runtime_entry_bridge_symbol(plan, bridge_symbol, sizeof(bridge_symbol));
        snprintf(text + strlen(text),
                 cap - strlen(text),
                 "entry\t%s\t%s\t%s\t%zu\t%zu\t%zu\n",
                 bridge_symbol,
                 entry_function->function_name,
                 entry_function->source_path,
                 entry_function->signature_line_number,
                 entry_function->body_first_line_number,
                 entry_function->body_last_line_number);
    }
    if (!v3_ensure_parent_dir(map_path) || !v3_write_text_file(map_path, text)) {
        free(text);
        return false;
    }
    free(text);
    return true;
}

static const V3LoweredFunctionStub *v3_find_entry_lowered_function(const V3LoweringPlanStub *lowering) {
    size_t i;
    for (i = 0; i < lowering->function_count; ++i) {
        if (lowering->functions[i].is_entry) {
            return &lowering->functions[i];
        }
    }
    return NULL;
}

static const V3LoweredFunctionStub *v3_find_lowered_function_same_module(const V3LoweringPlanStub *lowering,
                                                                         const V3LoweredFunctionStub *owner,
                                                                         const char *function_name) {
    int32_t found = v3_find_unique_lowered_function_index_by_module_and_name(lowering,
                                                                             owner->owner_module_path,
                                                                             function_name);
    if (found >= 0) {
        return &lowering->functions[found];
    }
    return NULL;
}

static const V3LoweredFunctionStub *v3_find_lowered_function_for_tail_call(const V3LoweringPlanStub *lowering,
                                                                           const V3LoweredFunctionStub *owner) {
    if (owner->callee_count > 0U) {
        int32_t found = v3_find_lowered_function_index_by_symbol(lowering,
                                                                 owner->callee_symbols[0].text);
        if (found >= 0) {
            return &lowering->functions[found];
        }
    }
    return v3_find_lowered_function_same_module(lowering,
                                                owner,
                                                owner->body_call_target);
}

static bool v3_abi_class_scalar_or_ptr(const char *abi_class) {
    return strcmp(abi_class, "i32") == 0 ||
           strcmp(abi_class, "i64") == 0 ||
           strcmp(abi_class, "bool") == 0 ||
           strcmp(abi_class, "ptr") == 0 ||
           strcmp(abi_class, "void") == 0;
}

static void v3_type_text_from_abi_class(const char *abi_class,
                                        char *type_out,
                                        size_t type_cap) {
    if (strcmp(abi_class, "i32") == 0) {
        snprintf(type_out, type_cap, "%s", "int32");
        return;
    }
    if (strcmp(abi_class, "i64") == 0) {
        snprintf(type_out, type_cap, "%s", "int64");
        return;
    }
    if (strcmp(abi_class, "bool") == 0) {
        snprintf(type_out, type_cap, "%s", "bool");
        return;
    }
    if (strcmp(abi_class, "ptr") == 0) {
        snprintf(type_out, type_cap, "%s", "ptr");
        return;
    }
    if (strcmp(abi_class, "void") == 0) {
        snprintf(type_out, type_cap, "%s", "void");
        return;
    }
    type_out[0] = '\0';
}

static void v3_text_append(char *out, size_t cap, const char *text) {
    strncat(out, text, cap - strlen(out) - 1U);
}

static void v3_text_appendf(char *out, size_t cap, const char *fmt, ...) {
    va_list ap;
    size_t used = strlen(out);
    if (used + 1U >= cap) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(out + used, cap - used, fmt, ap);
    va_end(ap);
}

static bool v3_trim_outer_parens(const char *expr_text, char *out, size_t cap) {
    char expr[8192];
    size_t len;
    int32_t depth = 0;
    bool in_string = false;
    size_t i;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    len = strlen(expr);
    if (len < 2U || expr[0] != '(' || expr[len - 1U] != ')') {
        return false;
    }
    for (i = 0; i < len - 1U; ++i) {
        char ch = expr[i];
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            depth += 1;
        } else if (ch == ')') {
            depth -= 1;
            if (depth == 0 && i != len - 1U) {
                return false;
            }
        }
    }
    snprintf(out, cap, "%.*s", (int)(len - 2U), expr + 1);
    v3_trim_copy_text(out, out, cap);
    return true;
}

static int32_t v3_find_top_level_binary_op_last(const char *text, const char *op) {
    bool in_string = false;
    int32_t depth = 0;
    int32_t bracket_depth = 0;
    size_t len = strlen(text);
    size_t op_len = strlen(op);
    size_t i;
    if (len < op_len) {
        return -1;
    }
    for (i = len; i-- > 0U; ) {
        char ch = text[i];
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == ')') {
            depth += 1;
            continue;
        }
        if (ch == '(') {
            depth -= 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth += 1;
            continue;
        }
        if (ch == '[') {
            bracket_depth -= 1;
            continue;
        }
        if (depth == 0 &&
            bracket_depth == 0 &&
            i + op_len <= len &&
            strncmp(text + i, op, op_len) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool v3_parse_prefix_single_arg_call(const char *expr_text,
                                            char *callee,
                                            size_t callee_cap,
                                            char *arg_text,
                                            size_t arg_cap) {
    char expr[8192];
    bool in_string = false;
    int32_t depth = 0;
    size_t i;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    for (i = 0; expr[i] != '\0'; ++i) {
        char ch = expr[i];
        if (ch == '"') {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            depth += 1;
            continue;
        }
        if (ch == ')') {
            depth -= 1;
            continue;
        }
        if (depth == 0 && isspace((unsigned char)ch)) {
            snprintf(callee, callee_cap, "%.*s", (int)i, expr);
            snprintf(arg_text, arg_cap, "%s", expr + i + 1U);
            v3_trim_copy_text(callee, callee, callee_cap);
            v3_trim_copy_text(arg_text, arg_text, arg_cap);
            if (strchr(callee, '(') != NULL ||
                strchr(callee, ')') != NULL ||
                strchr(callee, '[') != NULL ||
                strchr(callee, ']') != NULL) {
                return false;
            }
            if (arg_text[0] == '<' ||
                arg_text[0] == '>' ||
                arg_text[0] == '+' ||
                arg_text[0] == '-' ||
                arg_text[0] == '*' ||
                arg_text[0] == '/' ||
                arg_text[0] == '%' ||
                arg_text[0] == '&' ||
                arg_text[0] == '|' ||
                arg_text[0] == '^' ||
                arg_text[0] == '=' ||
                arg_text[0] == '!' ||
                arg_text[0] == '?' ||
                arg_text[0] == ':') {
                return false;
            }
            return callee[0] != '\0' && arg_text[0] != '\0';
        }
    }
    return false;
}

static bool v3_parse_binding_meta(const char *statement,
                                  const char *prefix,
                                  char *name_out,
                                  size_t name_cap,
                                  char *type_out,
                                  size_t type_cap,
                                  char *expr_out,
                                  size_t expr_cap) {
    const char *cursor = statement + strlen(prefix);
    const char *eq = strchr(cursor, '=');
    char left[256];
    char *colon;
    if (!eq) {
        return false;
    }
    snprintf(left, sizeof(left), "%.*s", (int)(eq - cursor), cursor);
    v3_trim_copy_text(left, left, sizeof(left));
    colon = strchr(left, ':');
    if (colon) {
        *colon = '\0';
        snprintf(type_out, type_cap, "%s", colon + 1);
        v3_trim_copy_text(type_out, type_out, type_cap);
    } else {
        type_out[0] = '\0';
    }
    snprintf(name_out, name_cap, "%s", left);
    v3_trim_copy_text(name_out, name_out, name_cap);
    snprintf(expr_out, expr_cap, "%s", eq + 1);
    v3_trim_copy_text(expr_out, expr_out, expr_cap);
    return name_out[0] != '\0' && expr_out[0] != '\0';
}

static bool v3_parse_binding_decl_meta(const char *statement,
                                       const char *prefix,
                                       char *name_out,
                                       size_t name_cap,
                                       char *type_out,
                                       size_t type_cap) {
    const char *cursor = statement + strlen(prefix);
    char left[256];
    char *colon;
    if (strchr(cursor, '=') != NULL) {
        return false;
    }
    snprintf(left, sizeof(left), "%s", cursor);
    v3_trim_copy_text(left, left, sizeof(left));
    colon = strchr(left, ':');
    if (!colon) {
        return false;
    }
    *colon = '\0';
    snprintf(name_out, name_cap, "%s", left);
    v3_trim_copy_text(name_out, name_out, name_cap);
    snprintf(type_out, type_cap, "%s", colon + 1);
    v3_trim_copy_text(type_out, type_out, type_cap);
    return name_out[0] != '\0' && type_out[0] != '\0';
}

static int32_t v3_find_top_level_assignment_eq(const char *text) {
    bool in_string = false;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    size_t i;
    for (i = 0; text && text[i] != '\0'; ++i) {
        char ch = text[i];
        if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            paren_depth += 1;
            continue;
        }
        if (ch == ')') {
            paren_depth -= 1;
            continue;
        }
        if (ch == '[') {
            bracket_depth += 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth -= 1;
            continue;
        }
        if (paren_depth == 0 && bracket_depth == 0 && ch == '=') {
            char prev = i > 0 ? text[i - 1] : '\0';
            char next = text[i + 1];
            if (prev == '=' || prev == '!' || prev == '<' || prev == '>' || next == '=') {
                continue;
            }
            return (int32_t)i;
        }
    }
    return -1;
}

static bool v3_parse_assignment_meta(const char *statement,
                                     char *lhs_out,
                                     size_t lhs_cap,
                                     char *rhs_out,
                                     size_t rhs_cap) {
    int32_t eq = v3_find_top_level_assignment_eq(statement);
    if (eq < 0) {
        return false;
    }
    snprintf(lhs_out, lhs_cap, "%.*s", eq, statement);
    v3_trim_copy_text(lhs_out, lhs_out, lhs_cap);
    snprintf(rhs_out, rhs_cap, "%s", statement + eq + 1);
    v3_trim_copy_text(rhs_out, rhs_out, rhs_cap);
    return lhs_out[0] != '\0' && rhs_out[0] != '\0';
}

static bool v3_parse_string_literal_text(const char *expr_text,
                                         char *text_out,
                                         size_t text_cap) {
    char expr[4096];
    size_t len;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    len = strlen(expr);
    if (len < 2U || expr[0] != '"' || expr[len - 1U] != '"') {
        return false;
    }
    snprintf(text_out, text_cap, "%.*s", (int)(len - 2U), expr + 1);
    return true;
}

static bool v3_parse_char_literal_text(const char *expr_text, int32_t *value_out) {
    char expr[64];
    size_t len;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    len = strlen(expr);
    if (len < 3U || expr[0] != '\'' || expr[len - 1U] != '\'') {
        return false;
    }
    if (len == 3U) {
        if (value_out) {
            *value_out = (unsigned char)expr[1];
        }
        return true;
    }
    if (len == 4U && expr[1] == '\\') {
        int32_t value = 0;
        switch (expr[2]) {
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case '0': value = '\0'; break;
            case '\\': value = '\\'; break;
            case '\'': value = '\''; break;
            case '"': value = '"'; break;
            default: return false;
        }
        if (value_out) {
            *value_out = value;
        }
        return true;
    }
    return false;
}

static bool v3_parse_int_literal_text(const char *expr_text, int64_t *value_out) {
    char expr[128];
    char *end = NULL;
    long long parsed;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return false;
    }
    parsed = strtoll(expr, &end, 0);
    if (!end || *v3_trim_inplace(end) != '\0') {
        return false;
    }
    if (value_out) {
        *value_out = (int64_t)parsed;
    }
    return true;
}

static int32_t v3_call_expr_string_literal_count(const char *expr_text) {
    char callee[PATH_MAX];
    char args[CHENG_V3_MAX_CALL_ARGS][4096];
    size_t arg_count = 0U;
    size_t i;
    int32_t count = 0;
    if (!v3_parse_call_text(expr_text, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
        return 0;
    }
    for (i = 0; i < arg_count; ++i) {
        char literal[4096];
        if (v3_parse_string_literal_text(args[i], literal, sizeof(literal))) {
            count += 1;
        }
    }
    return count;
}

static void v3_call_arg_temp_name(const V3LoweredFunctionStub *function,
                                  const char *callee,
                                  size_t arg_index,
                                  const char *expr_text,
                                  char *out,
                                  size_t cap) {
    char trimmed_expr[4096];
    char key[4608];
    uint64_t hash;
    v3_trim_copy_text(expr_text, trimmed_expr, sizeof(trimmed_expr));
    snprintf(key,
             sizeof(key),
             "%s|%s|%zu|%s",
             function->symbol_text,
             callee,
             arg_index,
             trimmed_expr);
    hash = v3_fnv1a64(key);
    snprintf(out, cap, "__v3_calltmp_%016llx", (unsigned long long)hash);
}

static void v3_update_max_i32(int32_t *value_io, int32_t candidate) {
    if (candidate > *value_io) {
        *value_io = candidate;
    }
}

static int32_t v3_find_local_slot(const V3AsmLocalSlot *locals,
                                  size_t local_count,
                                  const char *name) {
    size_t i;
    for (i = 0; i < local_count; ++i) {
        if (strcmp(locals[i].name, name) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool v3_add_local_slot_sized(V3AsmLocalSlot *locals,
                                    size_t *local_count,
                                    size_t local_cap,
                                    const char *name,
                                    const char *type_text,
                                    const char *abi_class,
                                    bool indirect_value,
                                    int32_t slot_size,
                                    int32_t slot_align,
                                    int32_t *next_offset_io) {
    int32_t align = slot_align > 0 ? slot_align : 1;
    int32_t size = slot_size > 0 ? slot_size : 8;
    if (*local_count >= local_cap) {
        return false;
    }
    snprintf(locals[*local_count].name, sizeof(locals[*local_count].name), "%s", name);
    snprintf(locals[*local_count].type_text, sizeof(locals[*local_count].type_text), "%s", type_text);
    snprintf(locals[*local_count].abi_class, sizeof(locals[*local_count].abi_class), "%s", abi_class);
    locals[*local_count].indirect_value = indirect_value;
    locals[*local_count].stack_align = align;
    locals[*local_count].stack_size = size;
    *next_offset_io = v3_align_up_i32(*next_offset_io, align);
    locals[*local_count].stack_offset = *next_offset_io;
    *next_offset_io += v3_align_up_i32(size, 8);
    *local_count += 1U;
    return true;
}

static bool v3_add_local_slot_for_type(const V3LoweringPlanStub *lowering,
                                       const char *owner_module_path,
                                       const V3ImportAlias *aliases,
                                       size_t alias_count,
                                       V3AsmLocalSlot *locals,
                                       size_t *local_count,
                                       size_t local_cap,
                                       const char *name,
                                       const char *type_text,
                                       const char *abi_class,
                                       bool as_indirect_param,
                                       int32_t *next_offset_io) {
    V3TypeLayoutStub layout;
    char normalized_type[256];
    if (!v3_normalize_type_text(lowering,
                                owner_module_path,
                                aliases,
                                alias_count,
                                type_text,
                                normalized_type,
                                sizeof(normalized_type))) {
        fprintf(stderr,
                "[cheng_v3_seed] normalize type failed owner=%s type=%s\n",
                owner_module_path,
                type_text);
        return false;
    }
    if (strcmp(abi_class, "void") == 0) {
        return false;
    }
    if (v3_abi_class_scalar_or_ptr(abi_class)) {
        return v3_add_local_slot_sized(locals,
                                       local_count,
                                       local_cap,
                                       name,
                                       normalized_type,
                                       abi_class,
                                       false,
                                       8,
                                       8,
                                       next_offset_io);
    }
    if (!v3_compute_type_layout_impl(lowering, normalized_type, &layout, 0U)) {
        fprintf(stderr,
                "[cheng_v3_seed] layout failed owner=%s type=%s normalized=%s\n",
                owner_module_path,
                type_text,
                normalized_type);
        return false;
    }
    return v3_add_local_slot_sized(locals,
                                   local_count,
                                   local_cap,
                                   name,
                                   normalized_type,
                                   abi_class,
                                   as_indirect_param,
                                   as_indirect_param ? 8 : layout.size,
                                   as_indirect_param ? 8 : layout.align,
                                   next_offset_io);
}

static bool v3_emit_sp_offset_address(char *out, size_t cap, int dst_reg, int32_t offset);
static bool v3_emit_slot_address(char *out,
                                 size_t cap,
                                 const V3AsmLocalSlot *slot,
                                 int reg_index);
static bool v3_emit_sp_store_reg(char *out,
                                 size_t cap,
                                 int32_t offset,
                                 const char *abi_class,
                                 int src_reg);
static bool v3_emit_sp_load_reg(char *out,
                                size_t cap,
                                int32_t offset,
                                const char *abi_class,
                                int dst_reg);

static void v3_emit_load_slot(char *out,
                              size_t cap,
                              const V3AsmLocalSlot *slot,
                              int reg_index) {
    if (!slot || slot->stack_offset < 0 || !v3_emit_sp_offset_address(out, cap, 16, slot->stack_offset)) {
        abort();
    }
    if (slot->indirect_value || strcmp(slot->abi_class, "ptr") == 0) {
        v3_text_appendf(out, cap, "  ldr x%d, [x16]\n", reg_index);
    } else if (strcmp(slot->abi_class, "i32") == 0) {
        v3_text_appendf(out, cap, "  ldrsw x%d, [x16]\n", reg_index);
    } else if (strcmp(slot->abi_class, "bool") == 0) {
        v3_text_appendf(out, cap, "  ldr w%d, [x16]\n", reg_index);
    } else {
        v3_text_appendf(out, cap, "  ldr x%d, [x16]\n", reg_index);
    }
}

static void v3_emit_store_slot(char *out,
                               size_t cap,
                               const V3AsmLocalSlot *slot,
                               int reg_index) {
    if (!slot || slot->stack_offset < 0 || !v3_emit_sp_offset_address(out, cap, 16, slot->stack_offset)) {
        abort();
    }
    if (slot->indirect_value || strcmp(slot->abi_class, "ptr") == 0) {
        v3_text_appendf(out, cap, "  str x%d, [x16]\n", reg_index);
    } else if (strcmp(slot->abi_class, "i32") == 0 || strcmp(slot->abi_class, "bool") == 0) {
        v3_text_appendf(out, cap, "  str w%d, [x16]\n", reg_index);
    } else {
        v3_text_appendf(out, cap, "  str x%d, [x16]\n", reg_index);
    }
}

static bool v3_external_call_signature(const char *name, V3AsmCallTarget *target) {
    static const struct {
        const char *name;
        const char *symbol;
        const char *ret;
        size_t argc;
        const char *args[8];
    } TABLE[] = {
        {"rawmemAsVoidPtrBridge", "rawmemAsVoid", "ptr", 1U, {"ptr"}},
        {"rawmemAsVoidCStringBridge", "rawmemAsVoid", "ptr", 1U, {"ptr"}},
        {"rawmemCopyRaw", "copyMem", "void", 3U, {"ptr", "ptr", "i32"}},
        {"rawmemSet", "setMem", "void", 3U, {"ptr", "i32", "i32"}},
        {"rawmemPtrAdd", "ptr_add", "ptr", 2U, {"ptr", "i32"}},
        {"puts", "puts", "i32", 1U, {"ptr"}},
        {"c_puts", "c_puts", "i32", 1U, {"ptr"}},
        {"exit", "exit", "void", 1U, {"i32"}},
        {"c_strlen", "cheng_cstrlen", "i32", 1U, {"ptr"}},
        {"cheng_str_param_to_cstring_compat", "cheng_str_param_to_cstring_compat", "ptr", 1U, {"ptr"}},
        {"alloc", "alloc", "ptr", 1U, {"i32"}},
        {"cheng_seq_set_grow", "cheng_seq_set_grow", "ptr", 3U, {"ptr", "i32", "i32"}},
        {"chengRawbytesGetAt", "cheng_rawbytes_get_at", "i32", 2U, {"ptr", "i32"}},
        {"chengRawbytesSetAt", "cheng_rawbytes_set_at", "void", 3U, {"ptr", "i32", "i32"}},
        {"driver_c_i32_to_str", "driver_c_i32_to_str", "ptr", 1U, {"i32"}},
        {"driver_c_i64_to_str", "driver_c_i64_to_str", "ptr", 1U, {"i64"}},
        {"driver_c_u64_to_str", "driver_c_u64_to_str", "ptr", 1U, {"i64"}},
        {"intToStrRaw", "driver_c_i32_to_str", "ptr", 1U, {"i32"}}
    };
    const char *lookup = name;
    const char *dot = strrchr(name, '.');
    const char *scope = strstr(name, "::");
    size_t i;
    if (scope && (!dot || scope > dot)) {
        lookup = scope + 2;
    } else if (dot) {
        lookup = dot + 1;
    }
    for (i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); ++i) {
        size_t j;
        if (strcmp(TABLE[i].name, lookup) != 0) {
            continue;
        }
        memset(target, 0, sizeof(*target));
        target->external = true;
        snprintf(target->callee_name, sizeof(target->callee_name), "%s", lookup);
        snprintf(target->symbol_name, sizeof(target->symbol_name), "_%s", TABLE[i].symbol);
        v3_type_text_from_abi_class(TABLE[i].ret, target->return_type, sizeof(target->return_type));
        snprintf(target->return_abi_class, sizeof(target->return_abi_class), "%s", TABLE[i].ret);
        target->param_count = TABLE[i].argc;
        for (j = 0; j < TABLE[i].argc; ++j) {
            v3_type_text_from_abi_class(TABLE[i].args[j],
                                        target->param_types[j],
                                        sizeof(target->param_types[j]));
            snprintf(target->param_abi_classes[j],
                     sizeof(target->param_abi_classes[j]),
                     "%s",
                     TABLE[i].args[j]);
        }
        return true;
    }
    return false;
}

static bool v3_resolve_call_target(const V3SystemLinkPlanStub *plan,
                                   const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *current_function,
                                   const V3ImportAlias *aliases,
                                   size_t alias_count,
                                   const char *callee,
                                   V3AsmCallTarget *target) {
    char symbol_text[PATH_MAX];
    int32_t function_index;
    bool resolved = false;
    symbol_text[0] = '\0';
    if (strchr(callee, '.') != NULL) {
        char callee_copy[PATH_MAX];
        char *dot;
        const char *module_path;
        snprintf(callee_copy, sizeof(callee_copy), "%s", callee);
        dot = strchr(callee_copy, '.');
        *dot = '\0';
        module_path = v3_alias_module_path(aliases, alias_count, callee_copy);
        if (module_path) {
            snprintf(symbol_text, sizeof(symbol_text), "%s::%s", module_path, dot + 1);
            resolved = true;
        }
    } else if (!v3_is_intrinsic_call_name(callee) &&
               v3_resolve_bare_call_symbol(lowering,
                                           current_function->owner_module_path,
                                           aliases,
                                           alias_count,
                                           callee,
                                           symbol_text,
                                           sizeof(symbol_text))) {
        resolved = true;
    } else if (v3_const_resolve_call_symbol(aliases,
                                            alias_count,
                                            current_function,
                                            callee,
                                            symbol_text,
                                            sizeof(symbol_text))) {
        resolved = true;
    }
    if (resolved) {
        function_index = v3_find_lowered_function_index_by_symbol(lowering, symbol_text);
        if (function_index >= 0) {
            const V3LoweredFunctionStub *callee_function = &lowering->functions[function_index];
            V3ImportAlias callee_aliases[CHENG_V3_MAX_IMPORT_ALIASES];
            size_t callee_alias_count = 0U;
            size_t i;
            if (!v3_load_import_aliases_from_source_path(callee_function->source_path,
                                                         callee_aliases,
                                                         &callee_alias_count,
                                                         CHENG_V3_MAX_IMPORT_ALIASES)) {
                fprintf(stderr,
                        "[cheng_v3_seed] callee alias load failed: %s\n",
                        callee_function->source_path);
                return false;
            }
            memset(target, 0, sizeof(*target));
            target->external = false;
            target->lowered_function = callee_function;
            snprintf(target->callee_name, sizeof(target->callee_name), "%s", callee_function->function_name);
            v3_primary_symbol_name(plan, callee_function, target->symbol_name, sizeof(target->symbol_name));
            if (!v3_normalize_type_text(lowering,
                                        callee_function->owner_module_path,
                                        callee_aliases,
                                        callee_alias_count,
                                        callee_function->return_type,
                                        target->return_type,
                                        sizeof(target->return_type))) {
                fprintf(stderr,
                        "[cheng_v3_seed] callee return type normalize failed function=%s type=%s\n",
                        callee_function->symbol_text,
                        callee_function->return_type);
                return false;
            }
            snprintf(target->return_abi_class,
                     sizeof(target->return_abi_class),
                     "%s",
                     v3_type_abi_class(target->return_type));
            target->param_count = callee_function->param_count;
            for (i = 0; i < callee_function->param_count && i < CHENG_V3_MAX_CALL_ARGS; ++i) {
                target->param_is_var[i] = callee_function->param_is_var[i];
                if (!v3_normalize_type_text(lowering,
                                            callee_function->owner_module_path,
                                            callee_aliases,
                                            callee_alias_count,
                                            callee_function->param_types[i],
                                            target->param_types[i],
                                            sizeof(target->param_types[i]))) {
                    fprintf(stderr,
                            "[cheng_v3_seed] callee param type normalize failed function=%s param=%s type=%s\n",
                            callee_function->symbol_text,
                            callee_function->param_names[i],
                            callee_function->param_types[i]);
                    return false;
                }
                snprintf(target->param_abi_classes[i],
                         sizeof(target->param_abi_classes[i]),
                         "%s",
                         v3_type_abi_class(target->param_types[i]));
            }
            return true;
        }
    }
    return v3_external_call_signature(callee, target);
}

static void v3_emit_call_arg_spill_store(char *out,
                                         size_t cap,
                                         int32_t call_arg_base,
                                         int call_depth,
                                         size_t arg_index,
                                         const char *abi_class,
                                         int src_reg_index) {
    int32_t offset = call_arg_base + call_depth * CHENG_V3_CALL_ARG_SPILL_STRIDE + (int32_t)arg_index * 8;
    if (!v3_emit_sp_store_reg(out, cap, offset, abi_class, src_reg_index)) {
        abort();
    }
}

static void v3_emit_call_arg_spill_load(char *out,
                                        size_t cap,
                                        int32_t call_arg_base,
                                        int call_depth,
                                        size_t arg_index,
                                        int dest_reg_index,
                                        const char *abi_class) {
    int32_t offset = call_arg_base + call_depth * CHENG_V3_CALL_ARG_SPILL_STRIDE + (int32_t)arg_index * 8;
    if (!v3_emit_sp_load_reg(out, cap, offset, abi_class, dest_reg_index)) {
        abort();
    }
}

static void v3_emit_call_dest_spill_store(char *out,
                                          size_t cap,
                                          int32_t call_arg_base,
                                          int call_depth,
                                          int src_reg_index) {
    int32_t offset =
        call_arg_base + call_depth * CHENG_V3_CALL_ARG_SPILL_STRIDE + (int32_t)CHENG_V3_MAX_CALL_ARGS * 8;
    if (!v3_emit_sp_store_reg(out, cap, offset, "ptr", src_reg_index)) {
        abort();
    }
}

static void v3_emit_call_dest_spill_load(char *out,
                                         size_t cap,
                                         int32_t call_arg_base,
                                         int call_depth,
                                         int dest_reg_index) {
    int32_t offset =
        call_arg_base + call_depth * CHENG_V3_CALL_ARG_SPILL_STRIDE + (int32_t)CHENG_V3_MAX_CALL_ARGS * 8;
    if (!v3_emit_sp_load_reg(out, cap, offset, "ptr", dest_reg_index)) {
        abort();
    }
}

static bool v3_emit_cstring_literal_label(const V3LoweredFunctionStub *current_function,
                                          const char *literal_text,
                                          int32_t *string_label_index_io,
                                          char *label_out,
                                          size_t label_cap,
                                          char *out,
                                          size_t cap) {
    char safe_name[128];
    char *escaped;
    v3_sanitize_path_part(current_function->function_name, safe_name, sizeof(safe_name));
    snprintf(label_out,
             label_cap,
             "L_v3_strlit_%s_%d",
             safe_name,
             *string_label_index_io);
    *string_label_index_io += 1;
    escaped = v3_c_escape(literal_text);
    if (!escaped) {
        return false;
    }
    v3_text_appendf(out, cap,
                    ".section __TEXT,__cstring,cstring_literals\n"
                    "%s:\n"
                    "  .asciz \"%s\"\n"
                    ".text\n"
                    ".p2align 2\n",
                    label_out,
                    escaped);
    free(escaped);
    return true;
}

static bool v3_emit_str_literal_into_slot(const V3LoweredFunctionStub *current_function,
                                          const V3AsmLocalSlot *dest_slot,
                                          const char *literal_text,
                                          int32_t *string_label_index_io,
                                          char *out,
                                          size_t cap) {
    char label[128];
    if (!dest_slot || strcmp(dest_slot->type_text, "str") != 0 || dest_slot->stack_offset < 0) {
        return false;
    }
    if (!v3_emit_cstring_literal_label(current_function,
                                       literal_text,
                                       string_label_index_io,
                                       label,
                                       sizeof(label),
                                       out,
                                       cap)) {
        return false;
    }
    if (!v3_emit_slot_address(out, cap, dest_slot, 15)) {
        return false;
    }
    v3_text_appendf(out, cap,
                    "  adrp x9, %s@PAGE\n"
                    "  add x9, x9, %s@PAGEOFF\n"
                    "  str x9, [x15, #0]\n"
                    "  mov w10, #%d\n"
                    "  str w10, [x15, #8]\n"
                    "  str wzr, [x15, #12]\n"
                    "  str wzr, [x15, #16]\n"
                    "  str wzr, [x15, #20]\n",
                    label,
                    label,
                    (int)strlen(literal_text));
    return true;
}

static bool v3_emit_str_literal_into_address(const V3LoweredFunctionStub *current_function,
                                             const char *literal_text,
                                             int dest_addr_reg,
                                             int32_t *string_label_index_io,
                                             char *out,
                                             size_t cap) {
    char label[128];
    if (!v3_emit_cstring_literal_label(current_function,
                                       literal_text,
                                       string_label_index_io,
                                       label,
                                       sizeof(label),
                                       out,
                                       cap)) {
        return false;
    }
    v3_text_appendf(out, cap,
                    "  adrp x9, %s@PAGE\n"
                    "  add x9, x9, %s@PAGEOFF\n"
                    "  str x9, [x%d, #0]\n"
                    "  mov w10, #%d\n"
                    "  str w10, [x%d, #8]\n"
                    "  str wzr, [x%d, #12]\n"
                    "  str wzr, [x%d, #16]\n"
                    "  str wzr, [x%d, #20]\n",
                    label,
                    label,
                    dest_addr_reg,
                    (int)strlen(literal_text),
                    dest_addr_reg,
                    dest_addr_reg,
                    dest_addr_reg,
                    dest_addr_reg);
    return true;
}

static bool v3_emit_slot_address(char *out,
                                 size_t cap,
                                 const V3AsmLocalSlot *slot,
                                 int reg_index);
static bool v3_emit_add_address_offset(char *out,
                                       size_t cap,
                                       int dst_reg,
                                       int src_reg,
                                       int32_t offset);
static bool v3_emit_sp_store_reg(char *out,
                                 size_t cap,
                                 int32_t offset,
                                 const char *abi_class,
                                 int src_reg);
static bool v3_emit_sp_load_reg(char *out,
                                size_t cap,
                                int32_t offset,
                                const char *abi_class,
                                int dst_reg);

static bool v3_emit_zero_address_range(char *out,
                                       size_t cap,
                                       int addr_reg,
                                       int32_t size) {
    int32_t offset = 0;
    while (offset + 8 <= size) {
        v3_text_appendf(out, cap, "  str xzr, [x%d, #%d]\n", addr_reg, offset);
        offset += 8;
    }
    if (offset + 4 <= size) {
        v3_text_appendf(out, cap, "  str wzr, [x%d, #%d]\n", addr_reg, offset);
        offset += 4;
    }
    while (offset < size) {
        v3_text_appendf(out, cap, "  strb wzr, [x%d, #%d]\n", addr_reg, offset);
        offset += 1;
    }
    return true;
}

static bool v3_emit_zero_slot(char *out,
                              size_t cap,
                              const V3AsmLocalSlot *slot) {
    if (!slot) {
        return false;
    }
    if (!v3_emit_slot_address(out, cap, slot, 15)) {
        return false;
    }
    return v3_emit_zero_address_range(out, cap, 15, slot->stack_size);
}

static void v3_emit_store_scalar_to_address(char *out,
                                            size_t cap,
                                            int addr_reg,
                                            int32_t offset,
                                            const char *abi_class,
                                            int src_reg) {
    if (strcmp(abi_class, "bool") == 0) {
        v3_text_appendf(out, cap, "  strb w%d, [x%d, #%d]\n", src_reg, addr_reg, offset);
    } else if (strcmp(abi_class, "i32") == 0) {
        v3_text_appendf(out, cap, "  str w%d, [x%d, #%d]\n", src_reg, addr_reg, offset);
    } else {
        v3_text_appendf(out, cap, "  str x%d, [x%d, #%d]\n", src_reg, addr_reg, offset);
    }
}

static bool v3_emit_copy_address_to_address(char *out,
                                            size_t cap,
                                            int dst_reg,
                                            int src_reg,
                                            int32_t size) {
    if (size < 0) {
        return false;
    }
    v3_text_appendf(out, cap,
                    "  mov x0, x%d\n"
                    "  mov x1, x%d\n"
                    "  mov w2, #%d\n"
                    "  bl _copyMem\n",
                    dst_reg,
                    src_reg,
                    size);
    return true;
}

static bool v3_emit_copy_slot_to_address(const V3LoweringPlanStub *lowering,
                                         char *out,
                                         size_t cap,
                                         const V3AsmLocalSlot *src_slot,
                                         int dst_reg) {
    V3TypeLayoutStub layout;
    char value_type[256];
    int32_t copy_size;
    int src_reg;
    if (!src_slot || src_slot->stack_size < 0) {
        return false;
    }
    if (src_slot->indirect_value) {
        if (!lowering) {
            return false;
        }
        v3_strip_var_type_text(src_slot->type_text, value_type, sizeof(value_type));
        if (!v3_compute_type_layout_impl(lowering, value_type, &layout, 0U)) {
            return false;
        }
        copy_size = layout.size;
    } else {
        copy_size = src_slot->stack_size;
    }
    src_reg = dst_reg == 14 ? 15 : 14;
    if (!v3_emit_slot_address(out, cap, src_slot, src_reg)) {
        return false;
    }
    return v3_emit_copy_address_to_address(out, cap, dst_reg, src_reg, copy_size);
}

static bool v3_emit_mov_imm(char *out, size_t cap, int reg_index, int64_t value) {
    uint64_t bits = (uint64_t)value;
    bool emitted = false;
    int shift;
    if (value >= -65535 && value <= 65535) {
        v3_text_appendf(out, cap, "  mov x%d, #%lld\n", reg_index, (long long)value);
        return true;
    }
    for (shift = 0; shift <= 48; shift += 16) {
        uint16_t part = (uint16_t)((bits >> shift) & 0xffffU);
        if (!emitted) {
            if (part == 0U && shift < 48) {
                continue;
            }
            v3_text_appendf(out,
                            cap,
                            "  movz x%d, #%u, lsl #%d\n",
                            reg_index,
                            (unsigned)part,
                            shift);
            emitted = true;
            continue;
        }
        if (part != 0U) {
            v3_text_appendf(out,
                            cap,
                            "  movk x%d, #%u, lsl #%d\n",
                            reg_index,
                            (unsigned)part,
                            shift);
        }
    }
    if (!emitted) {
        v3_text_appendf(out, cap, "  mov x%d, #0\n", reg_index);
    }
    return true;
}

static bool v3_parse_member_access_expr(const char *expr_text,
                                        char *base_out,
                                        size_t base_cap,
                                        char *field_out,
                                        size_t field_cap) {
    char expr[4096];
    bool in_string = false;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    int32_t last_dot = -1;
    size_t i;
    size_t len;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    len = strlen(expr);
    if (len <= 0U) {
        return false;
    }
    for (i = 0; i < len; ++i) {
        char ch = expr[i];
        if (ch == '"' && (i == 0U || expr[i - 1U] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            paren_depth += 1;
            continue;
        }
        if (ch == ')') {
            paren_depth -= 1;
            if (paren_depth < 0) {
                return false;
            }
            continue;
        }
        if (ch == '[') {
            bracket_depth += 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth -= 1;
            if (bracket_depth < 0) {
                return false;
            }
            continue;
        }
        if (ch == '.' && paren_depth == 0 && bracket_depth == 0) {
            last_dot = (int32_t)i;
        }
    }
    if (in_string || paren_depth != 0 || bracket_depth != 0 || last_dot <= 0 || (size_t)last_dot + 1U >= len) {
        return false;
    }
    snprintf(base_out, base_cap, "%.*s", last_dot, expr);
    v3_trim_copy_text(base_out, base_out, base_cap);
    snprintf(field_out, field_cap, "%s", expr + last_dot + 1);
    v3_trim_copy_text(field_out, field_out, field_cap);
    if (base_out[0] == '\0' ||
        field_out[0] == '\0' ||
        strchr(field_out, '.') != NULL ||
        strchr(field_out, '[') != NULL ||
        strchr(field_out, ']') != NULL ||
        strchr(field_out, '(') != NULL ||
        strchr(field_out, ')') != NULL ||
        strchr(field_out, ' ') != NULL) {
        return false;
    }
    return true;
}

static bool v3_parse_index_access_expr(const char *expr_text,
                                       char *base_out,
                                       size_t base_cap,
                                       char *index_out,
                                       size_t index_cap) {
    char expr[4096];
    bool in_string = false;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    int32_t top_open = -1;
    size_t i;
    size_t len;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    len = strlen(expr);
    if (len < 3U || expr[len - 1U] != ']') {
        return false;
    }
    for (i = 0; i < len; ++i) {
        char ch = expr[i];
        if (ch == '"' && (i == 0 || expr[i - 1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            paren_depth += 1;
            continue;
        }
        if (ch == ')') {
            paren_depth -= 1;
            if (paren_depth < 0) {
                return false;
            }
            continue;
        }
        if (ch == '[') {
            if (paren_depth == 0 && bracket_depth == 0) {
                top_open = (int32_t)i;
            }
            bracket_depth += 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth -= 1;
            if (bracket_depth < 0) {
                return false;
            }
            continue;
        }
    }
    if (in_string || paren_depth != 0 || bracket_depth != 0 || top_open <= 0) {
        return false;
    }
    snprintf(base_out, base_cap, "%.*s", top_open, expr);
    v3_trim_copy_text(base_out, base_out, base_cap);
    snprintf(index_out, index_cap, "%.*s", (int)(len - (size_t)top_open - 2U), expr + top_open + 1);
    v3_trim_copy_text(index_out, index_out, index_cap);
    return base_out[0] != '\0' && index_out[0] != '\0';
}

static bool v3_find_type_def_by_normalized(const V3LoweringPlanStub *lowering,
                                           const char *normalized_type,
                                           int32_t *index_out) {
    char owner_module[PATH_MAX];
    char type_name[128];
    char *sep;
    if (index_out) {
        *index_out = -1;
    }
    if (strstr(normalized_type, "::") == NULL) {
        return false;
    }
    snprintf(owner_module, sizeof(owner_module), "%s", normalized_type);
    sep = strrchr(owner_module, ':');
    if (!sep || sep == owner_module || sep[-1] != ':') {
        return false;
    }
    sep[-1] = '\0';
    snprintf(type_name, sizeof(type_name), "%s", sep + 1);
    if (index_out) {
        *index_out = v3_find_type_def_index_by_symbol(lowering, owner_module, type_name);
    }
    return index_out && *index_out >= 0;
}

static bool v3_resolve_field_meta_impl(const V3LoweringPlanStub *lowering,
                                       const char *normalized_type,
                                       const char *field_name,
                                       char *field_type_out,
                                       size_t field_type_cap,
                                       char *field_abi_out,
                                       size_t field_abi_cap,
                                       int32_t *field_offset_out,
                                       unsigned depth) {
    int32_t type_index = -1;
    if (depth > 8U) {
        return false;
    }
    if (strcmp(normalized_type, "Bytes") == 0) {
        if (strcmp(field_name, "data") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "ptr");
            snprintf(field_abi_out, field_abi_cap, "%s", "ptr");
            *field_offset_out = 0;
            return true;
        }
        if (strcmp(field_name, "len") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "int32");
            snprintf(field_abi_out, field_abi_cap, "%s", "i32");
            *field_offset_out = 8;
            return true;
        }
        return false;
    }
    if (strcmp(normalized_type, "str") == 0) {
        if (strcmp(field_name, "data") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "ptr");
            snprintf(field_abi_out, field_abi_cap, "%s", "ptr");
            *field_offset_out = 0;
            return true;
        }
        if (strcmp(field_name, "len") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "int32");
            snprintf(field_abi_out, field_abi_cap, "%s", "i32");
            *field_offset_out = 8;
            return true;
        }
        if (strcmp(field_name, "storeId") == 0 || strcmp(field_name, "store_id") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "int32");
            snprintf(field_abi_out, field_abi_cap, "%s", "i32");
            *field_offset_out = 12;
            return true;
        }
        if (strcmp(field_name, "flags") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "int32");
            snprintf(field_abi_out, field_abi_cap, "%s", "i32");
            *field_offset_out = 16;
            return true;
        }
        return false;
    }
    if (strlen(normalized_type) > 2U &&
        strcmp(normalized_type + strlen(normalized_type) - 2U, "[]") == 0) {
        if (strcmp(field_name, "len") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "int32");
            snprintf(field_abi_out, field_abi_cap, "%s", "i32");
            *field_offset_out = 0;
            return true;
        }
        if (strcmp(field_name, "cap") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "int32");
            snprintf(field_abi_out, field_abi_cap, "%s", "i32");
            *field_offset_out = 4;
            return true;
        }
        if (strcmp(field_name, "buffer") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "ptr");
            snprintf(field_abi_out, field_abi_cap, "%s", "ptr");
            *field_offset_out = 8;
            return true;
        }
        return false;
    }
    if (v3_startswith(normalized_type, "Result[") && normalized_type[strlen(normalized_type) - 1U] == ']') {
        V3TypeLayoutStub value_layout;
        char value_type[256];
        int32_t value_offset;
        snprintf(value_type,
                 sizeof(value_type),
                 "%.*s",
                 (int)(strlen(normalized_type) - 8U),
                 normalized_type + 7);
        if (!v3_compute_type_layout_impl(lowering, value_type, &value_layout, depth + 1U)) {
            return false;
        }
        value_offset = v3_align_up_i32(1, value_layout.align);
        if (strcmp(field_name, "ok") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "bool");
            snprintf(field_abi_out, field_abi_cap, "%s", "bool");
            *field_offset_out = 0;
            return true;
        }
        if (strcmp(field_name, "value") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", value_type);
            snprintf(field_abi_out, field_abi_cap, "%s", v3_type_abi_class(value_type));
            *field_offset_out = value_offset;
            return true;
        }
        if (strcmp(field_name, "err") == 0) {
            snprintf(field_type_out, field_type_cap, "%s", "std/result::ErrorInfo");
            snprintf(field_abi_out, field_abi_cap, "%s", "composite");
            *field_offset_out = v3_align_up_i32(value_offset + value_layout.size, 8);
            return true;
        }
        return false;
    }
    if (!v3_find_type_def_by_normalized(lowering, normalized_type, &type_index)) {
        return false;
    }
    if (lowering->type_defs[type_index].kind == V3_TYPE_DEF_ALIAS) {
        return v3_resolve_field_meta_impl(lowering,
                                          lowering->type_defs[type_index].alias_target,
                                          field_name,
                                          field_type_out,
                                          field_type_cap,
                                          field_abi_out,
                                          field_abi_cap,
                                          field_offset_out,
                                          depth + 1U);
    }
    if (lowering->type_defs[type_index].kind != V3_TYPE_DEF_RECORD) {
        return false;
    }
    {
        int32_t offset = 0;
        int32_t i;
        for (i = 0; i < (int32_t)lowering->type_defs[type_index].field_count; ++i) {
            V3TypeLayoutStub field_layout;
            int32_t field_align;
            if (!v3_compute_type_layout_impl(lowering,
                                             lowering->type_defs[type_index].fields[i].type_text,
                                             &field_layout,
                                             depth + 1U)) {
                return false;
            }
            field_align = field_layout.align > 0 ? field_layout.align : 1;
            offset = v3_align_up_i32(offset, field_align);
            if (strcmp(lowering->type_defs[type_index].fields[i].name, field_name) == 0) {
                snprintf(field_type_out,
                         field_type_cap,
                         "%s",
                         lowering->type_defs[type_index].fields[i].type_text);
                snprintf(field_abi_out,
                         field_abi_cap,
                         "%s",
                         v3_type_abi_class(field_type_out));
                *field_offset_out = offset;
                return true;
            }
            offset += field_layout.size;
        }
    }
    return false;
}

static bool v3_resolve_field_meta(const V3LoweringPlanStub *lowering,
                                  const char *base_type_text,
                                  const char *field_name,
                                  char *field_type_out,
                                  size_t field_type_cap,
                                  char *field_abi_out,
                                  size_t field_abi_cap,
                                  int32_t *field_offset_out) {
    char normalized_type[256];
    v3_strip_var_prefix(base_type_text, normalized_type, sizeof(normalized_type));
    return v3_resolve_field_meta_impl(lowering,
                                      normalized_type,
                                      field_name,
                                      field_type_out,
                                      field_type_cap,
                                      field_abi_out,
                                      field_abi_cap,
                                      field_offset_out,
                                      0U);
}

static bool v3_resolve_field_path_expr(const V3LoweringPlanStub *lowering,
                                       const V3AsmLocalSlot *locals,
                                       size_t local_count,
                                       const char *expr_text,
                                       int32_t *base_local_index_out,
                                       char *field_type_out,
                                       size_t field_type_cap,
                                       char *field_abi_out,
                                       size_t field_abi_cap,
                                       int32_t *field_offset_out) {
    char expr[4096];
    char *segments[32];
    size_t segment_count = 0U;
    char *cursor;
    char current_type[256];
    int32_t local_index;
    int32_t total_offset = 0;
    size_t i;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0' ||
        strchr(expr, '(') != NULL ||
        strchr(expr, '[') != NULL ||
        strchr(expr, ' ') != NULL) {
        return false;
    }
    cursor = expr;
    segments[segment_count++] = cursor;
    while (*cursor != '\0') {
        if (*cursor == '.') {
            *cursor = '\0';
            if (segment_count >= 32U || cursor[1] == '\0') {
                return false;
            }
            segments[segment_count++] = cursor + 1;
        }
        cursor += 1;
    }
    if (segment_count < 2U) {
        return false;
    }
    local_index = v3_find_local_slot(locals, local_count, segments[0]);
    if (local_index < 0) {
        return false;
    }
    snprintf(current_type, sizeof(current_type), "%s", locals[local_index].type_text);
    for (i = 1; i < segment_count; ++i) {
        int32_t current_offset = 0;
        char next_type[256];
        char next_abi[32];
        if (!v3_resolve_field_meta(lowering,
                                   current_type,
                                   segments[i],
                                   next_type,
                                   sizeof(next_type),
                                   next_abi,
                                   sizeof(next_abi),
                                   &current_offset)) {
            return false;
        }
        total_offset += current_offset;
        snprintf(current_type, sizeof(current_type), "%s", next_type);
        snprintf(field_abi_out, field_abi_cap, "%s", next_abi);
    }
    if (base_local_index_out) {
        *base_local_index_out = local_index;
    }
    if (field_type_out) {
        snprintf(field_type_out, field_type_cap, "%s", current_type);
    }
    if (field_offset_out) {
        *field_offset_out = total_offset;
    }
    return true;
}

static bool v3_emit_index_access_address(const V3SystemLinkPlanStub *plan,
                                         const V3LoweringPlanStub *lowering,
                                         const V3LoweredFunctionStub *current_function,
                                         const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         const V3AsmLocalSlot *locals,
                                         size_t local_count,
                                         const char *expr_text,
                                         char *field_type_out,
                                         size_t field_type_cap,
                                         char *field_abi_out,
                                         size_t field_abi_cap,
                                         int addr_reg,
                                         int32_t call_arg_base,
                                         int32_t string_temp_base,
                                         int32_t string_temp_stride,
                                         int call_depth,
                                         char *out,
                                         size_t cap);
static bool v3_infer_expr_type(const V3SystemLinkPlanStub *plan,
                               const V3LoweringPlanStub *lowering,
                               const V3LoweredFunctionStub *current_function,
                               const V3ImportAlias *aliases,
                               size_t alias_count,
                               const V3AsmLocalSlot *locals,
                               size_t local_count,
                               const char *expr_text,
                               char *type_out,
                               size_t type_cap,
                               char *abi_out,
                               size_t abi_cap);

static bool v3_emit_field_path_address(char *out,
                                       size_t cap,
                                       const V3AsmLocalSlot *locals,
                                       size_t local_count,
                                       const V3LoweringPlanStub *lowering,
                                       const char *expr_text,
                                       char *field_type_out,
                                       size_t field_type_cap,
                                       char *field_abi_out,
                                       size_t field_abi_cap,
                                       int addr_reg) {
    int32_t base_local_index = -1;
    int32_t field_offset = 0;
    if (!v3_resolve_field_path_expr(lowering,
                                    locals,
                                    local_count,
                                    expr_text,
                                    &base_local_index,
                                    field_type_out,
                                    field_type_cap,
                                    field_abi_out,
                                    field_abi_cap,
                                    &field_offset) ||
        !v3_emit_slot_address(out, cap, &locals[base_local_index], addr_reg)) {
        return false;
    }
    if (!v3_emit_add_address_offset(out, cap, addr_reg, addr_reg, field_offset)) {
        return false;
    }
    return true;
}

static bool v3_emit_member_access_address(const V3SystemLinkPlanStub *plan,
                                          const V3LoweringPlanStub *lowering,
                                          const V3LoweredFunctionStub *current_function,
                                          const V3ImportAlias *aliases,
                                          size_t alias_count,
                                          const V3AsmLocalSlot *locals,
                                          size_t local_count,
                                          const char *expr_text,
                                          char *field_type_out,
                                          size_t field_type_cap,
                                          char *field_abi_out,
                                          size_t field_abi_cap,
                                          int addr_reg,
                                          int32_t call_arg_base,
                                          int32_t string_temp_base,
                                          int32_t string_temp_stride,
                                          int call_depth,
                                          char *out,
                                          size_t cap) {
    char base_expr[4096];
    char field_name[128];
    char base_type[256];
    char base_abi[32];
    char nested_type[256];
    char nested_abi[32];
    int32_t field_offset = 0;
    int32_t local_index = -1;
    if (!v3_parse_member_access_expr(expr_text,
                                     base_expr,
                                     sizeof(base_expr),
                                     field_name,
                                     sizeof(field_name)) ||
        !v3_infer_expr_type(plan,
                            lowering,
                            current_function,
                            aliases,
                            alias_count,
                            locals,
                            local_count,
                            base_expr,
                            base_type,
                            sizeof(base_type),
                            base_abi,
                            sizeof(base_abi)) ||
        !v3_resolve_field_meta(lowering,
                               base_type,
                               field_name,
                               field_type_out,
                               field_type_cap,
                               field_abi_out,
                               field_abi_cap,
                               &field_offset)) {
        return false;
    }
    local_index = v3_find_local_slot(locals, local_count, base_expr);
    if (local_index >= 0) {
        if (!v3_emit_slot_address(out, cap, &locals[local_index], addr_reg)) {
            return false;
        }
    } else if (!v3_emit_index_access_address(plan,
                                             lowering,
                                             current_function,
                                             aliases,
                                             alias_count,
                                             locals,
                                             local_count,
                                             base_expr,
                                             nested_type,
                                             sizeof(nested_type),
                                             nested_abi,
                                             sizeof(nested_abi),
                                             addr_reg,
                                             call_arg_base,
                                             string_temp_base,
                                             string_temp_stride,
                                             call_depth + 1,
                                             out,
                                             cap) &&
               !v3_emit_member_access_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              base_expr,
                                              nested_type,
                                              sizeof(nested_type),
                                              nested_abi,
                                              sizeof(nested_abi),
                                              addr_reg,
                                              call_arg_base,
                                              string_temp_base,
                                              string_temp_stride,
                                              call_depth + 1,
                                              out,
                                              cap) &&
               !v3_emit_field_path_address(out,
                                           cap,
                                           locals,
                                           local_count,
                                           lowering,
                                           base_expr,
                                           nested_type,
                                           sizeof(nested_type),
                                           nested_abi,
                                           sizeof(nested_abi),
                                           addr_reg)) {
        return false;
    }
    if (!v3_emit_add_address_offset(out, cap, addr_reg, addr_reg, field_offset)) {
        return false;
    }
    return true;
}

static bool v3_infer_expr_type(const V3SystemLinkPlanStub *plan,
                               const V3LoweringPlanStub *lowering,
                               const V3LoweredFunctionStub *current_function,
                               const V3ImportAlias *aliases,
                               size_t alias_count,
                               const V3AsmLocalSlot *locals,
                               size_t local_count,
                               const char *expr_text,
                               char *type_out,
                               size_t type_cap,
                               char *abi_out,
                               size_t abi_cap);
static bool v3_codegen_expr_scalar(const V3SystemLinkPlanStub *plan,
                                   const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *current_function,
                                   const V3ImportAlias *aliases,
                                   size_t alias_count,
                                   const V3AsmLocalSlot *locals,
                                   size_t local_count,
                                   const char *expr_text,
                                   const char *expected_abi,
                                   int target_reg,
                                   int32_t call_arg_base,
                                   int32_t string_temp_base,
                                   int32_t string_temp_stride,
                                   int call_depth,
                                   char *out,
                                   size_t cap);
static bool v3_parse_named_arg_meta(const char *arg_text,
                                    char *field_out,
                                    size_t field_cap,
                                    char *expr_out,
                                    size_t expr_cap);

static bool v3_emit_index_access_address(const V3SystemLinkPlanStub *plan,
                                         const V3LoweringPlanStub *lowering,
                                         const V3LoweredFunctionStub *current_function,
                                         const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         const V3AsmLocalSlot *locals,
                                         size_t local_count,
                                         const char *expr_text,
                                         char *field_type_out,
                                         size_t field_type_cap,
                                         char *field_abi_out,
                                         size_t field_abi_cap,
                                         int addr_reg,
                                         int32_t call_arg_base,
                                         int32_t string_temp_base,
                                         int32_t string_temp_stride,
                                         int call_depth,
                                         char *out,
                                         size_t cap) {
    char expr[4096];
    char base_expr[4096];
    char index_expr[4096];
    char base_type[256];
    char base_abi[32];
    char normalized_base_type[256];
    char elem_type[256];
    char elem_abi[32];
    V3TypeLayoutStub elem_layout;
    int32_t fixed_len = 0;
    int32_t local_index;
    int32_t elem_size = 0;
    int data_offset = -1;
    int base_reg = addr_reg == 13 ? 14 : 13;
    int index_reg = 11;
    int size_reg = 12;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (!v3_parse_index_access_expr(expr, base_expr, sizeof(base_expr), index_expr, sizeof(index_expr)) ||
        !v3_infer_expr_type(plan,
                            lowering,
                            current_function,
                            aliases,
                            alias_count,
                            locals,
                            local_count,
                            base_expr,
                            base_type,
                            sizeof(base_type),
                            base_abi,
                            sizeof(base_abi)) ||
        !v3_normalize_type_text(lowering,
                                current_function->owner_module_path,
                                aliases,
                                alias_count,
                                base_type,
                                normalized_base_type,
                                sizeof(normalized_base_type))) {
        return false;
    }
    if (strcmp(normalized_base_type, "Bytes") == 0) {
        snprintf(elem_type, sizeof(elem_type), "%s", "int32");
        snprintf(elem_abi, sizeof(elem_abi), "%s", "i32");
        elem_size = 1;
        data_offset = 0;
    } else if (strcmp(normalized_base_type, "str") == 0) {
        snprintf(elem_type, sizeof(elem_type), "%s", "char");
        snprintf(elem_abi, sizeof(elem_abi), "%s", "i32");
        elem_size = 1;
        data_offset = 0;
    } else if (strlen(normalized_base_type) > 2U &&
               strcmp(normalized_base_type + strlen(normalized_base_type) - 2U, "[]") == 0) {
        snprintf(elem_type,
                 sizeof(elem_type),
                 "%.*s",
                 (int)(strlen(normalized_base_type) - 2U),
                 normalized_base_type);
        snprintf(elem_abi, sizeof(elem_abi), "%s", v3_type_abi_class(elem_type));
        if (!v3_compute_type_layout_impl(lowering, elem_type, &elem_layout, 0U)) {
            return false;
        }
        elem_size = elem_layout.size;
        data_offset = 8;
    } else if (v3_parse_fixed_array_type(normalized_base_type, elem_type, sizeof(elem_type), &fixed_len)) {
        (void)fixed_len;
        snprintf(elem_abi, sizeof(elem_abi), "%s", v3_type_abi_class(elem_type));
        if (!v3_compute_type_layout_impl(lowering, elem_type, &elem_layout, 0U)) {
            return false;
        }
        elem_size = elem_layout.size;
    } else {
        return false;
    }
    local_index = v3_find_local_slot(locals, local_count, base_expr);
    if (local_index >= 0) {
        if (v3_abi_class_scalar_or_ptr(locals[local_index].abi_class) &&
            strcmp(normalized_base_type, "Bytes") != 0 &&
            strcmp(normalized_base_type, "str") != 0) {
            return false;
        }
        if (!v3_emit_slot_address(out, cap, &locals[local_index], base_reg)) {
            return false;
        }
    } else {
        char nested_type[256];
        char nested_abi[32];
        if (!v3_emit_member_access_address(plan,
                                           lowering,
                                           current_function,
                                           aliases,
                                           alias_count,
                                           locals,
                                           local_count,
                                           base_expr,
                                           nested_type,
                                           sizeof(nested_type),
                                           nested_abi,
                                           sizeof(nested_abi),
                                           base_reg,
                                           call_arg_base,
                                           string_temp_base,
                                           string_temp_stride,
                                           call_depth + 1,
                                           out,
                                           cap) &&
            !v3_emit_index_access_address(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          base_expr,
                                          nested_type,
                                          sizeof(nested_type),
                                          nested_abi,
                                          sizeof(nested_abi),
                                          base_reg,
                                          call_arg_base,
                                          string_temp_base,
                                          string_temp_stride,
                                          call_depth + 1,
                                          out,
                                          cap) &&
            !v3_emit_field_path_address(out,
                                        cap,
                                        locals,
                                        local_count,
                                        lowering,
                                        base_expr,
                                        nested_type,
                                        sizeof(nested_type),
                                        nested_abi,
                                        sizeof(nested_abi),
                                        base_reg)) {
            return false;
        }
    }
    if (data_offset >= 0) {
        v3_text_appendf(out, cap, "  ldr x%d, [x%d, #%d]\n", base_reg, base_reg, data_offset);
    }
    if (!v3_codegen_expr_scalar(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                index_expr,
                                "i64",
                                index_reg,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                call_depth + 1,
                                out,
                                cap)) {
        return false;
    }
    if (elem_size <= 0) {
        return false;
    }
    if (elem_size != 1) {
        if (!v3_emit_mov_imm(out, cap, size_reg, elem_size)) {
            return false;
        }
        v3_text_appendf(out, cap, "  mul x%d, x%d, x%d\n", index_reg, index_reg, size_reg);
    }
    v3_text_appendf(out, cap, "  add x%d, x%d, x%d\n", addr_reg, base_reg, index_reg);
    if (field_type_out) {
        snprintf(field_type_out, field_type_cap, "%s", elem_type);
    }
    if (field_abi_out) {
        snprintf(field_abi_out, field_abi_cap, "%s", elem_abi);
    }
    return true;
}

static bool v3_infer_lvalue_expr_type(const V3SystemLinkPlanStub *plan,
                                      const V3LoweringPlanStub *lowering,
                                      const V3LoweredFunctionStub *current_function,
                                      const V3ImportAlias *aliases,
                                      size_t alias_count,
                                      const V3AsmLocalSlot *locals,
                                      size_t local_count,
                                      const char *expr_text,
                                      char *field_type_out,
                                      size_t field_type_cap,
                                      char *field_abi_out,
                                      size_t field_abi_cap) {
    char base_expr[4096];
    char field_name[128];
    char index_expr[4096];
    char base_type[256];
    char base_abi[32];
    char normalized_base_type[256];
    int32_t field_offset = 0;
    int32_t base_local_index = -1;
    int32_t fixed_len = 0;
    if (v3_resolve_field_path_expr(lowering,
                                   locals,
                                   local_count,
                                   expr_text,
                                   &base_local_index,
                                   field_type_out,
                                   field_type_cap,
                                   field_abi_out,
                                   field_abi_cap,
                                   &field_offset)) {
        return true;
    }
    if (v3_parse_member_access_expr(expr_text,
                                    base_expr,
                                    sizeof(base_expr),
                                    field_name,
                                    sizeof(field_name)) &&
        v3_infer_expr_type(plan,
                           lowering,
                           current_function,
                           aliases,
                           alias_count,
                           locals,
                           local_count,
                           base_expr,
                           base_type,
                           sizeof(base_type),
                           base_abi,
                           sizeof(base_abi)) &&
        v3_resolve_field_meta(lowering,
                              base_type,
                              field_name,
                              field_type_out,
                              field_type_cap,
                              field_abi_out,
                              field_abi_cap,
                              &field_offset)) {
        return true;
    }
    if (!v3_parse_index_access_expr(expr_text,
                                    base_expr,
                                    sizeof(base_expr),
                                    index_expr,
                                    sizeof(index_expr)) ||
        !v3_infer_expr_type(plan,
                            lowering,
                            current_function,
                            aliases,
                            alias_count,
                            locals,
                            local_count,
                            base_expr,
                            base_type,
                            sizeof(base_type),
                            base_abi,
                            sizeof(base_abi)) ||
        !v3_normalize_type_text(lowering,
                                current_function->owner_module_path,
                                aliases,
                                alias_count,
                                base_type,
                                normalized_base_type,
                                sizeof(normalized_base_type))) {
        return false;
    }
    if (strcmp(normalized_base_type, "Bytes") == 0) {
        snprintf(field_type_out, field_type_cap, "%s", "int32");
        snprintf(field_abi_out, field_abi_cap, "%s", "i32");
        return true;
    }
    if (strcmp(normalized_base_type, "str") == 0) {
        snprintf(field_type_out, field_type_cap, "%s", "char");
        snprintf(field_abi_out, field_abi_cap, "%s", "i32");
        return true;
    }
    if (strlen(normalized_base_type) > 2U &&
        strcmp(normalized_base_type + strlen(normalized_base_type) - 2U, "[]") == 0) {
        snprintf(field_type_out,
                 field_type_cap,
                 "%.*s",
                 (int)(strlen(normalized_base_type) - 2U),
                 normalized_base_type);
        snprintf(field_abi_out, field_abi_cap, "%s", v3_type_abi_class(field_type_out));
        return true;
    }
    if (v3_parse_fixed_array_type(normalized_base_type,
                                  field_type_out,
                                  field_type_cap,
                                  &fixed_len)) {
        snprintf(field_abi_out, field_abi_cap, "%s", v3_type_abi_class(field_type_out));
        return true;
    }
    return false;
}

static bool v3_emit_lvalue_address(const V3SystemLinkPlanStub *plan,
                                   const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *current_function,
                                   const V3ImportAlias *aliases,
                                   size_t alias_count,
                                   const V3AsmLocalSlot *locals,
                                   size_t local_count,
                                   const char *expr_text,
                                   char *field_type_out,
                                   size_t field_type_cap,
                                   char *field_abi_out,
                                   size_t field_abi_cap,
                                   int addr_reg,
                                   int32_t call_arg_base,
                                   int32_t string_temp_base,
                                   int32_t string_temp_stride,
                                   int call_depth,
                                   char *out,
                                   size_t cap) {
    if (v3_emit_member_access_address(plan,
                                      lowering,
                                      current_function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      expr_text,
                                      field_type_out,
                                      field_type_cap,
                                      field_abi_out,
                                      field_abi_cap,
                                      addr_reg,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      call_depth,
                                      out,
                                      cap) ||
        v3_emit_field_path_address(out,
                                   cap,
                                   locals,
                                   local_count,
                                   lowering,
                                   expr_text,
                                   field_type_out,
                                   field_type_cap,
                                   field_abi_out,
                                   field_abi_cap,
                                   addr_reg) ||
        v3_emit_index_access_address(plan,
                                     lowering,
                                     current_function,
                                     aliases,
                                     alias_count,
                                     locals,
                                     local_count,
                                     expr_text,
                                     field_type_out,
                                     field_type_cap,
                                     field_abi_out,
                                     field_abi_cap,
                                     addr_reg,
                                     call_arg_base,
                                     string_temp_base,
                                     string_temp_stride,
                                     call_depth,
                                     out,
                                     cap)) {
        return true;
    }
    return false;
}

static bool v3_prepare_expr_call_state(const V3SystemLinkPlanStub *plan,
                                       const V3LoweringPlanStub *lowering,
                                       const V3LoweredFunctionStub *current_function,
                                       const V3ImportAlias *aliases,
                                       size_t alias_count,
                                       V3AsmLocalSlot *locals,
                                       size_t *local_count,
                                       size_t local_cap,
                                       int32_t *next_offset_io,
                                       const char *expr_text,
                                       int call_depth,
                                       int32_t *max_call_depth_io) {
    static const char *OPS[] = {
        "||", "&&", "==", "!=", "<=", ">=", "<<", ">>", "<", ">", "|", "^", "&", "+", "-", "*", "/", "%"
    };
    char expr[8192];
    char outer[8192];
    char base_name[256];
    char field_name[128];
    char index_expr[4096];
    char ternary_cond[4096];
    char ternary_true[4096];
    char ternary_false[4096];
    char named_field[128];
    char named_expr[4096];
    char callee[PATH_MAX];
    char arg_text[4096];
    char args[CHENG_V3_MAX_CALL_ARGS][4096];
    size_t arg_count = 0U;
    bool list_nonempty = false;
    size_t i;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return true;
    }
    if (v3_parse_named_arg_meta(expr,
                                named_field,
                                sizeof(named_field),
                                named_expr,
                                sizeof(named_expr))) {
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          named_expr,
                                          call_depth,
                                          max_call_depth_io);
    }
    if (v3_trim_outer_parens(expr, outer, sizeof(outer))) {
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          outer,
                                          call_depth,
                                          max_call_depth_io);
    }
    if (expr[0] == '!') {
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          expr + 1,
                                          call_depth,
                                          max_call_depth_io);
    }
    if (v3_parse_ternary_expr(expr,
                              ternary_cond,
                              sizeof(ternary_cond),
                              ternary_true,
                              sizeof(ternary_true),
                              ternary_false,
                              sizeof(ternary_false))) {
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          ternary_cond,
                                          call_depth,
                                          max_call_depth_io) &&
               v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          ternary_true,
                                          call_depth,
                                          max_call_depth_io) &&
               v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          ternary_false,
                                          call_depth,
                                          max_call_depth_io);
    }
    for (i = 0; i < sizeof(OPS) / sizeof(OPS[0]); ++i) {
        int32_t op_index = v3_find_top_level_binary_op_last(expr, OPS[i]);
        if (op_index >= 0) {
            char left[4096];
            char right[4096];
            snprintf(left, sizeof(left), "%.*s", op_index, expr);
            snprintf(right, sizeof(right), "%s", expr + op_index + strlen(OPS[i]));
            v3_trim_copy_text(left, left, sizeof(left));
            v3_trim_copy_text(right, right, sizeof(right));
            return v3_prepare_expr_call_state(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              local_cap,
                                              next_offset_io,
                                              left,
                                              call_depth,
                                              max_call_depth_io) &&
                   v3_prepare_expr_call_state(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              local_cap,
                                              next_offset_io,
                                              right,
                                              call_depth,
                                              max_call_depth_io);
        }
    }
    if (v3_parse_member_access_expr(expr, base_name, sizeof(base_name), field_name, sizeof(field_name))) {
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          base_name,
                                          call_depth,
                                          max_call_depth_io);
    }
    if (v3_parse_index_access_expr(expr, base_name, sizeof(base_name), index_expr, sizeof(index_expr))) {
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          base_name,
                                          call_depth,
                                          max_call_depth_io) &&
               v3_prepare_expr_call_state(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                              index_expr,
                                              call_depth,
                                              max_call_depth_io);
    }
    if (v3_parse_list_literal_text(expr, args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
        for (i = 0; i < arg_count; ++i) {
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            current_function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            local_cap,
                                            next_offset_io,
                                            args[i],
                                            call_depth,
                                            max_call_depth_io)) {
                return false;
            }
        }
        return true;
    }
    if (v3_parse_call_text(expr, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS) ||
        v3_parse_prefix_single_arg_call(expr, callee, sizeof(callee), arg_text, sizeof(arg_text))) {
        V3AsmCallTarget target;
        bool has_target = false;
        int next_call_depth = call_depth + 1;
        if (arg_count == 0U && arg_text[0] != '\0') {
            snprintf(args[0], sizeof(args[0]), "%s", arg_text);
            arg_count = 1U;
        }
        v3_update_max_i32(max_call_depth_io, next_call_depth);
        has_target = v3_resolve_call_target(plan,
                                            lowering,
                                            current_function,
                                            aliases,
                                            alias_count,
                                            callee,
                                            &target);
        for (i = 0; i < arg_count; ++i) {
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            current_function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            local_cap,
                                            next_offset_io,
                                            args[i],
                                            next_call_depth,
                                            max_call_depth_io)) {
                return false;
            }
            if (has_target &&
                i < target.param_count &&
                !v3_abi_class_scalar_or_ptr(target.param_abi_classes[i])) {
                char literal_text[4096];
                char temp_name[128];
                char inferred_type[256];
                char inferred_abi[32];
                if (v3_parse_string_literal_text(args[i], literal_text, sizeof(literal_text))) {
                    continue;
                }
                if (v3_find_local_slot(locals, *local_count, args[i]) >= 0) {
                    continue;
                }
                v3_call_arg_temp_name(current_function,
                                      callee,
                                      i,
                                      args[i],
                                      temp_name,
                                      sizeof(temp_name));
                if (v3_find_local_slot(locals, *local_count, temp_name) >= 0) {
                    continue;
                }
                if (v3_is_list_literal_text(args[i], &list_nonempty) &&
                    strlen(target.param_types[i]) > 2U &&
                    strcmp(target.param_types[i] + strlen(target.param_types[i]) - 2U, "[]") == 0) {
                    snprintf(inferred_type, sizeof(inferred_type), "%s", target.param_types[i]);
                    snprintf(inferred_abi, sizeof(inferred_abi), "%s", target.param_abi_classes[i]);
                } else if (!v3_infer_expr_type(plan,
                                               lowering,
                                               current_function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               *local_count,
                                               args[i],
                                               inferred_type,
                                               sizeof(inferred_type),
                                               inferred_abi,
                                               sizeof(inferred_abi))) {
                    return false;
                }
                if (v3_abi_class_scalar_or_ptr(inferred_abi)) {
                    return false;
                }
                if (!v3_add_local_slot_for_type(lowering,
                                                current_function->owner_module_path,
                                                aliases,
                                                alias_count,
                                                locals,
                                                local_count,
                                                local_cap,
                                                temp_name,
                                                inferred_type,
                                                inferred_abi,
                                                false,
                                                next_offset_io)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static void v3_emit_reg_to_return(char *out,
                                  size_t cap,
                                  int src_reg_index,
                                  const char *abi_class) {
    if (strcmp(abi_class, "i32") == 0 || strcmp(abi_class, "bool") == 0) {
        v3_text_appendf(out, cap, "  mov w0, w%d\n", src_reg_index);
    } else if (strcmp(abi_class, "i64") == 0 || strcmp(abi_class, "ptr") == 0) {
        if (src_reg_index != 0) {
            v3_text_appendf(out, cap, "  mov x0, x%d\n", src_reg_index);
        }
    }
}

static bool v3_emit_sp_adjust(char *out, size_t cap, bool subtract, int32_t amount) {
    if (amount < 0) {
        return false;
    }
    if (amount == 0) {
        return true;
    }
    if (amount <= 4095) {
        v3_text_appendf(out, cap, "  %s sp, sp, #%d\n", subtract ? "sub" : "add", amount);
        return true;
    }
    if (!v3_emit_mov_imm(out, cap, 17, amount)) {
        return false;
    }
    v3_text_appendf(out, cap, "  %s sp, sp, x17\n", subtract ? "sub" : "add");
    return true;
}

static bool v3_emit_sp_offset_address(char *out, size_t cap, int dst_reg, int32_t offset) {
    if (offset < 0) {
        return false;
    }
    if (offset == 0) {
        v3_text_appendf(out, cap, "  mov x%d, sp\n", dst_reg);
        return true;
    }
    if (offset <= 4095) {
        v3_text_appendf(out, cap, "  add x%d, sp, #%d\n", dst_reg, offset);
        return true;
    }
    if (!v3_emit_mov_imm(out, cap, 17, offset)) {
        return false;
    }
    v3_text_appendf(out, cap, "  add x%d, sp, x17\n", dst_reg);
    return true;
}

static bool v3_emit_add_address_offset(char *out,
                                       size_t cap,
                                       int dst_reg,
                                       int src_reg,
                                       int32_t offset) {
    if (offset < 0) {
        return false;
    }
    if (offset == 0) {
        if (dst_reg != src_reg) {
            v3_text_appendf(out, cap, "  mov x%d, x%d\n", dst_reg, src_reg);
        }
        return true;
    }
    if (offset <= 4095) {
        v3_text_appendf(out, cap, "  add x%d, x%d, #%d\n", dst_reg, src_reg, offset);
        return true;
    }
    if (!v3_emit_mov_imm(out, cap, 17, offset)) {
        return false;
    }
    v3_text_appendf(out, cap, "  add x%d, x%d, x17\n", dst_reg, src_reg);
    return true;
}

static bool v3_emit_sp_store_reg(char *out,
                                 size_t cap,
                                 int32_t offset,
                                 const char *abi_class,
                                 int src_reg) {
    if (!v3_emit_sp_offset_address(out, cap, 16, offset)) {
        return false;
    }
    if (strcmp(abi_class, "i32") == 0 || strcmp(abi_class, "bool") == 0) {
        v3_text_appendf(out, cap, "  str w%d, [x16]\n", src_reg);
    } else {
        v3_text_appendf(out, cap, "  str x%d, [x16]\n", src_reg);
    }
    return true;
}

static bool v3_emit_sp_load_reg(char *out,
                                size_t cap,
                                int32_t offset,
                                const char *abi_class,
                                int dst_reg) {
    if (!v3_emit_sp_offset_address(out, cap, 16, offset)) {
        return false;
    }
    if (strcmp(abi_class, "i32") == 0 || strcmp(abi_class, "bool") == 0) {
        v3_text_appendf(out, cap, "  ldr w%d, [x16]\n", dst_reg);
    } else {
        v3_text_appendf(out, cap, "  ldr x%d, [x16]\n", dst_reg);
    }
    return true;
}

static bool v3_emit_function_epilogue(char *out, size_t cap, int32_t frame_size) {
    int32_t save_offset = frame_size - 16;
    if (save_offset < 0) {
        return false;
    }
    if (save_offset <= 504 && (save_offset % 8) == 0) {
        v3_text_appendf(out, cap, "  ldp x29, x30, [sp, #%d]\n", save_offset);
    } else if (!v3_emit_sp_offset_address(out, cap, 16, save_offset)) {
        return false;
    } else {
        v3_text_appendf(out, cap, "  ldp x29, x30, [x16]\n");
    }
    if (!v3_emit_sp_adjust(out, cap, false, frame_size)) {
        return false;
    }
    v3_text_appendf(out, cap, "  ret\n");
    return true;
}

static bool v3_codegen_expr_scalar(const V3SystemLinkPlanStub *plan,
                                   const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *current_function,
                                   const V3ImportAlias *aliases,
                                   size_t alias_count,
                                   const V3AsmLocalSlot *locals,
                                   size_t local_count,
                                   const char *expr_text,
                                   const char *expected_abi,
                                   int target_reg,
                                   int32_t call_arg_base,
                                   int32_t string_temp_base,
                                   int32_t string_temp_stride,
                                   int call_depth,
                                   char *out,
                                   size_t cap);
static bool v3_parse_result_inner_type(const char *result_type,
                                       char *inner_type_out,
                                       size_t inner_type_cap);
static bool v3_emit_result_field_address(const V3SystemLinkPlanStub *plan,
                                         const V3LoweringPlanStub *lowering,
                                         const V3LoweredFunctionStub *current_function,
                                         const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         const V3AsmLocalSlot *locals,
                                         size_t local_count,
                                         const char *arg_expr,
                                         const char *field_name,
                                         int addr_reg,
                                         char *out,
                                         size_t cap,
                                         char *field_type_out,
                                         size_t field_type_cap,
                                         char *field_abi_out,
                                         size_t field_abi_cap);
static bool v3_codegen_call_into_address(const V3SystemLinkPlanStub *plan,
                                         const V3LoweringPlanStub *lowering,
                                         const V3LoweredFunctionStub *current_function,
                                         const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         const V3AsmLocalSlot *locals,
                                         size_t local_count,
                                         const char *callee,
                                         char args[][4096],
                                         size_t arg_count,
                                         int dest_addr_reg,
                                         int32_t call_arg_base,
                                         int32_t string_temp_base,
                                         int32_t string_temp_stride,
                                         int32_t *string_label_index_io,
                                         int call_depth,
                                         char *out,
                                         size_t cap);
static bool v3_materialize_composite_expr_into_slot(const V3SystemLinkPlanStub *plan,
                                                    const V3LoweringPlanStub *lowering,
                                                    const V3LoweredFunctionStub *current_function,
                                                    const V3ImportAlias *aliases,
                                                    size_t alias_count,
                                                    const V3AsmLocalSlot *locals,
                                                    size_t local_count,
                                                    const char *callee,
                                                    size_t arg_index,
                                                    const char *expr_text,
                                                    const V3AsmLocalSlot *dest_slot,
                                                    int32_t call_arg_base,
                                                    int32_t string_temp_base,
                                                    int32_t string_temp_stride,
                                                    int32_t *string_label_index_io,
                                                    int call_depth,
                                                    char *out,
                                                    size_t cap);
static bool v3_materialize_composite_expr_into_address(const V3SystemLinkPlanStub *plan,
                                                       const V3LoweringPlanStub *lowering,
                                                       const V3LoweredFunctionStub *current_function,
                                                       const V3ImportAlias *aliases,
                                                       size_t alias_count,
                                                       const V3AsmLocalSlot *locals,
                                                       size_t local_count,
                                                       const char *expr_text,
                                                       const char *dest_type_text,
                                                       int dest_addr_reg,
                                                       int32_t call_arg_base,
                                                       int32_t string_temp_base,
                                                       int32_t string_temp_stride,
                                                       int32_t *string_label_index_io,
                                                       int call_depth,
                                                       char *out,
                                                       size_t cap);
static bool v3_emit_slot_address(char *out,
                                 size_t cap,
                                 const V3AsmLocalSlot *slot,
                                 int reg_index);

static bool v3_infer_expr_type(const V3SystemLinkPlanStub *plan,
                               const V3LoweringPlanStub *lowering,
                               const V3LoweredFunctionStub *current_function,
                               const V3ImportAlias *aliases,
                               size_t alias_count,
                               const V3AsmLocalSlot *locals,
                               size_t local_count,
                               const char *expr_text,
                               char *type_out,
                               size_t type_cap,
                               char *abi_out,
                               size_t abi_cap) {
    static const char *OPS[] = {
        "||", "&&", "==", "!=", "<=", ">=", "<<", ">>", "<", ">", "|", "^", "&", "+", "-", "*", "/", "%"
    };
    char expr[8192];
    char outer[8192];
    char callee[PATH_MAX];
    char arg_text[4096];
    char base_expr[4096];
    char index_expr[4096];
    char field_name[128];
    char ternary_cond[4096];
    char ternary_true[4096];
    char ternary_false[4096];
    char left[4096];
    char right[4096];
    char args[CHENG_V3_MAX_CALL_ARGS][4096];
    size_t arg_count = 0U;
    size_t i;
    int32_t local_index;
    int32_t char_value = 0;
    int64_t literal_value;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return false;
    }
    if (v3_trim_outer_parens(expr, outer, sizeof(outer))) {
        return v3_infer_expr_type(plan,
                                  lowering,
                                  current_function,
                                  aliases,
                                  alias_count,
                                  locals,
                                  local_count,
                                  outer,
                                  type_out,
                                  type_cap,
                                  abi_out,
                                  abi_cap);
    }
    if (expr[0] == '"' && expr[strlen(expr) - 1U] == '"') {
        snprintf(type_out, type_cap, "%s", "str");
        snprintf(abi_out, abi_cap, "%s", "composite");
        return true;
    }
    if (strcmp(expr, "true") == 0 || strcmp(expr, "false") == 0) {
        snprintf(type_out, type_cap, "%s", "bool");
        snprintf(abi_out, abi_cap, "%s", "bool");
        return true;
    }
    if (strcmp(expr, "nil") == 0) {
        snprintf(type_out, type_cap, "%s", "ptr");
        snprintf(abi_out, abi_cap, "%s", "ptr");
        return true;
    }
    if (v3_parse_char_literal_text(expr, &char_value)) {
        snprintf(type_out, type_cap, "%s", "char");
        snprintf(abi_out, abi_cap, "%s", "i32");
        return true;
    }
    if (expr[0] == '!') {
        char inner_type[256];
        char inner_abi[32];
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                expr + 1,
                                inner_type,
                                sizeof(inner_type),
                                inner_abi,
                                sizeof(inner_abi)) ||
            strcmp(inner_abi, "bool") != 0) {
            return false;
        }
        snprintf(type_out, type_cap, "%s", "bool");
        snprintf(abi_out, abi_cap, "%s", "bool");
        return true;
    }
    if (v3_parse_ternary_expr(expr,
                              ternary_cond,
                              sizeof(ternary_cond),
                              ternary_true,
                              sizeof(ternary_true),
                              ternary_false,
                              sizeof(ternary_false))) {
        char left_type[256];
        char left_abi[32];
        char right_type[256];
        char right_abi[32];
        char normalized_left_type[256];
        char normalized_right_type[256];
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                ternary_true,
                                left_type,
                                sizeof(left_type),
                                left_abi,
                                sizeof(left_abi)) ||
            !v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                ternary_false,
                                right_type,
                                sizeof(right_type),
                                right_abi,
                                sizeof(right_abi)) ||
            !v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    left_type,
                                    normalized_left_type,
                                    sizeof(normalized_left_type)) ||
            !v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    right_type,
                                    normalized_right_type,
                                    sizeof(normalized_right_type)) ||
            strcmp(normalized_left_type, normalized_right_type) != 0) {
            return false;
        }
        snprintf(type_out, type_cap, "%s", normalized_left_type);
        snprintf(abi_out, abi_cap, "%s", v3_type_abi_class(normalized_left_type));
        return true;
    }
    for (i = 0; i < sizeof(OPS) / sizeof(OPS[0]); ++i) {
        char left_type[256];
        char left_abi[32];
        char right_type[256];
        char right_abi[32];
        char normalized_left_type[256];
        char normalized_right_type[256];
        int32_t op_index = v3_find_top_level_binary_op_last(expr, OPS[i]);
        if (op_index < 0) {
            continue;
        }
        snprintf(left, sizeof(left), "%.*s", op_index, expr);
        snprintf(right, sizeof(right), "%s", expr + op_index + strlen(OPS[i]));
        v3_trim_copy_text(left, left, sizeof(left));
        v3_trim_copy_text(right, right, sizeof(right));
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                left,
                                left_type,
                                sizeof(left_type),
                                left_abi,
                                sizeof(left_abi)) ||
            !v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                right,
                                right_type,
                                sizeof(right_type),
                                right_abi,
                                sizeof(right_abi))) {
            return false;
        }
        if (strcmp(OPS[i], "||") == 0 || strcmp(OPS[i], "&&") == 0 ||
            strcmp(OPS[i], "==") == 0 || strcmp(OPS[i], "!=") == 0 ||
            strcmp(OPS[i], "<") == 0 || strcmp(OPS[i], "<=") == 0 ||
            strcmp(OPS[i], ">") == 0 || strcmp(OPS[i], ">=") == 0) {
            snprintf(type_out, type_cap, "%s", "bool");
            snprintf(abi_out, abi_cap, "%s", "bool");
            return true;
        }
        if (!v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    left_type,
                                    normalized_left_type,
                                    sizeof(normalized_left_type)) ||
            !v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    right_type,
                                    normalized_right_type,
                                    sizeof(normalized_right_type))) {
            return false;
        }
        if (strcmp(OPS[i], "+") == 0 &&
            (strcmp(normalized_left_type, "str") == 0 ||
             strcmp(normalized_right_type, "str") == 0)) {
            snprintf(type_out, type_cap, "%s", "str");
            snprintf(abi_out, abi_cap, "%s", "composite");
            return true;
        }
        if (!v3_abi_class_scalar_or_ptr(left_abi) || !v3_abi_class_scalar_or_ptr(right_abi)) {
            return false;
        }
        if (strcmp(left_abi, "i64") == 0 || strcmp(right_abi, "i64") == 0 ||
            strcmp(normalized_left_type, "int64") == 0 || strcmp(normalized_right_type, "int64") == 0) {
            snprintf(type_out, type_cap, "%s", "int64");
            snprintf(abi_out, abi_cap, "%s", "i64");
        } else {
            snprintf(type_out, type_cap, "%s", "int32");
            snprintf(abi_out, abi_cap, "%s", "i32");
        }
        return true;
    }
    if (v3_parse_list_literal_text(expr, args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
        char elem_type[256];
        char elem_abi[32];
        char normalized_elem_type[256];
        size_t i;
        if (arg_count == 0U) {
            return false;
        }
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                args[0],
                                elem_type,
                                sizeof(elem_type),
                                elem_abi,
                                sizeof(elem_abi)) ||
            !v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    elem_type,
                                    normalized_elem_type,
                                    sizeof(normalized_elem_type))) {
            return false;
        }
        for (i = 1; i < arg_count; ++i) {
            char item_type[256];
            char item_abi[32];
            char normalized_item_type[256];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    args[i],
                                    item_type,
                                    sizeof(item_type),
                                    item_abi,
                                    sizeof(item_abi)) ||
                !v3_normalize_type_text(lowering,
                                        current_function->owner_module_path,
                                        aliases,
                                        alias_count,
                                        item_type,
                                        normalized_item_type,
                                        sizeof(normalized_item_type)) ||
                strcmp(normalized_elem_type, normalized_item_type) != 0) {
                return false;
            }
        }
        snprintf(type_out, type_cap, "%s[]", normalized_elem_type);
        snprintf(abi_out, abi_cap, "%s", "composite");
        return true;
    }
    if (v3_parse_index_access_expr(expr, base_expr, sizeof(base_expr), index_expr, sizeof(index_expr))) {
        char base_type[256];
        char base_abi[32];
        char normalized_base_type[256];
        int32_t fixed_len = 0;
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                base_expr,
                                base_type,
                                sizeof(base_type),
                                base_abi,
                                sizeof(base_abi)) ||
            !v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    base_type,
                                    normalized_base_type,
                                    sizeof(normalized_base_type))) {
            return false;
        }
        if (strcmp(normalized_base_type, "Bytes") == 0) {
            snprintf(type_out, type_cap, "%s", "int32");
            snprintf(abi_out, abi_cap, "%s", "i32");
            return true;
        }
        if (strcmp(normalized_base_type, "str") == 0) {
            snprintf(type_out, type_cap, "%s", "char");
            snprintf(abi_out, abi_cap, "%s", "i32");
            return true;
        }
        if (strlen(normalized_base_type) > 2U &&
            strcmp(normalized_base_type + strlen(normalized_base_type) - 2U, "[]") == 0) {
            snprintf(type_out,
                     type_cap,
                     "%.*s",
                     (int)(strlen(normalized_base_type) - 2U),
                     normalized_base_type);
            snprintf(abi_out, abi_cap, "%s", v3_type_abi_class(type_out));
            return true;
        }
        if (v3_parse_fixed_array_type(normalized_base_type, type_out, type_cap, &fixed_len)) {
            snprintf(abi_out, abi_cap, "%s", v3_type_abi_class(type_out));
            return true;
        }
        return false;
    }
    if (v3_parse_member_access_expr(expr, base_expr, sizeof(base_expr), field_name, sizeof(field_name))) {
        char base_type[256];
        char base_abi[32];
        int32_t field_offset = 0;
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                base_expr,
                                base_type,
                                sizeof(base_type),
                                base_abi,
                                sizeof(base_abi)) ||
            !v3_resolve_field_meta(lowering,
                                   base_type,
                                   field_name,
                                   type_out,
                                   type_cap,
                                   abi_out,
                                   abi_cap,
                                   &field_offset)) {
            return false;
        }
        return true;
    }
    if (v3_resolve_field_path_expr(lowering,
                                   locals,
                                   local_count,
                                   expr,
                                   &local_index,
                                   type_out,
                                   type_cap,
                                   abi_out,
                                   abi_cap,
                                   NULL)) {
        return true;
    }
    local_index = v3_find_local_slot(locals, local_count, expr);
    if (local_index >= 0) {
        snprintf(type_out, type_cap, "%s", locals[local_index].type_text);
        snprintf(abi_out, abi_cap, "%s", locals[local_index].abi_class);
        return true;
    }
    {
        const V3TopLevelConstStub *top_level_const =
            v3_find_expr_top_level_const(lowering,
                                         current_function->owner_module_path,
                                         aliases,
                                         alias_count,
                                         expr);
        if (top_level_const) {
            snprintf(type_out, type_cap, "%s", top_level_const->type_text);
            snprintf(abi_out, abi_cap, "%s", top_level_const->abi_class);
            return true;
        }
    }
    if (v3_parse_int_literal_text(expr, &literal_value)) {
        if (literal_value >= INT32_MIN && literal_value <= INT32_MAX) {
            snprintf(type_out, type_cap, "%s", "int32");
            snprintf(abi_out, abi_cap, "%s", "i32");
        } else {
            snprintf(type_out, type_cap, "%s", "int64");
            snprintf(abi_out, abi_cap, "%s", "i64");
        }
        return true;
    }
    if (v3_parse_call_text(expr, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
        V3AsmCallTarget target;
        char ctor_name[128];
        const char *ctor_base = strrchr(callee, '.');
        V3ResultIntrinsicKind result_kind =
            v3_resolve_result_intrinsic_kind(lowering,
                                             current_function->owner_module_path,
                                             aliases,
                                             alias_count,
                                             callee);
        ctor_base = ctor_base ? ctor_base + 1 : callee;
        if ((result_kind == V3_RESULT_INTRINSIC_IS_ERR ||
             result_kind == V3_RESULT_INTRINSIC_IS_OK) && arg_count == 1U) {
            char arg_type[256];
            char arg_abi[32];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    args[0],
                                    arg_type,
                                    sizeof(arg_type),
                                    arg_abi,
                                    sizeof(arg_abi)) ||
                strcmp(arg_abi, "composite") != 0 ||
                !v3_startswith(arg_type, "Result[")) {
                return false;
            }
            snprintf(type_out, type_cap, "%s", "bool");
            snprintf(abi_out, abi_cap, "%s", "bool");
            return true;
        }
        if ((result_kind == V3_RESULT_INTRINSIC_VALUE ||
             result_kind == V3_RESULT_INTRINSIC_ERROR ||
             result_kind == V3_RESULT_INTRINSIC_ERROR_TEXT ||
             result_kind == V3_RESULT_INTRINSIC_ERROR_INFO_OF) && arg_count == 1U) {
            char arg_type[256];
            char arg_abi[32];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    args[0],
                                    arg_type,
                                    sizeof(arg_type),
                                    arg_abi,
                                    sizeof(arg_abi)) ||
                strcmp(arg_abi, "composite") != 0 ||
                !v3_startswith(arg_type, "Result[")) {
                return false;
            }
            if (result_kind == V3_RESULT_INTRINSIC_VALUE) {
                char inner_type[256];
                if (!v3_parse_result_inner_type(arg_type, inner_type, sizeof(inner_type))) {
                    return false;
                }
                snprintf(type_out, type_cap, "%s", inner_type);
                snprintf(abi_out, abi_cap, "%s", v3_type_abi_class(inner_type));
                return true;
            }
            if (result_kind == V3_RESULT_INTRINSIC_ERROR_INFO_OF) {
                snprintf(type_out, type_cap, "%s", "std/result::ErrorInfo");
                snprintf(abi_out, abi_cap, "%s", "composite");
                return true;
            }
            snprintf(type_out, type_cap, "%s", "str");
            snprintf(abi_out, abi_cap, "%s", "composite");
            return true;
        }
        if (result_kind == V3_RESULT_INTRINSIC_OK && arg_count == 1U) {
            char value_type[256];
            char value_abi[32];
            char normalized_value_type[256];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    args[0],
                                    value_type,
                                    sizeof(value_type),
                                    value_abi,
                                    sizeof(value_abi)) ||
                !v3_normalize_type_text(lowering,
                                        current_function->owner_module_path,
                                        aliases,
                                        alias_count,
                                        value_type,
                                        normalized_value_type,
                                        sizeof(normalized_value_type))) {
                return false;
            }
            snprintf(type_out, type_cap, "Result[%s]", normalized_value_type);
            snprintf(abi_out, abi_cap, "%s", "composite");
            return true;
        }
        if ((result_kind == V3_RESULT_INTRINSIC_ERR && arg_count == 1U) ||
            (result_kind == V3_RESULT_INTRINSIC_ERR_CODE && arg_count == 2U) ||
            (result_kind == V3_RESULT_INTRINSIC_ERR_INFO && arg_count == 1U)) {
            char explicit_inner_type[256];
            char normalized_inner_type[256];
            if (!v3_extract_trailing_type_arg_text(callee,
                                                   explicit_inner_type,
                                                   sizeof(explicit_inner_type)) ||
                !v3_normalize_type_text(lowering,
                                        current_function->owner_module_path,
                                        aliases,
                                        alias_count,
                                        explicit_inner_type,
                                        normalized_inner_type,
                                        sizeof(normalized_inner_type))) {
                return false;
            }
            snprintf(type_out, type_cap, "Result[%s]", normalized_inner_type);
            snprintf(abi_out, abi_cap, "%s", "composite");
            return true;
        }
        if (strcmp(callee, "len") == 0 && arg_count == 1U) {
            char arg_type[256];
            char arg_abi[32];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    args[0],
                                    arg_type,
                                    sizeof(arg_type),
                                    arg_abi,
                                    sizeof(arg_abi))) {
                return false;
            }
            if (strcmp(arg_type, "str") != 0 &&
                strcmp(arg_type, "Bytes") != 0 &&
                !(strlen(arg_type) > 2U && strcmp(arg_type + strlen(arg_type) - 2U, "[]") == 0)) {
                return false;
            }
            snprintf(type_out, type_cap, "%s", "int32");
            snprintf(abi_out, abi_cap, "%s", "i32");
            return true;
        }
        if ((strcmp(callee, "int64") == 0 ||
             strcmp(callee, "int32") == 0 ||
             strcmp(callee, "ptr") == 0 ||
             strcmp(callee, "cstring") == 0 ||
             strcmp(callee, "bool") == 0) && arg_count == 1U) {
            if (strcmp(callee, "int32") == 0) {
                snprintf(type_out, type_cap, "%s", "int32");
                snprintf(abi_out, abi_cap, "%s", "i32");
            } else if (strcmp(callee, "int64") == 0) {
                snprintf(type_out, type_cap, "%s", "int64");
                snprintf(abi_out, abi_cap, "%s", "i64");
            } else if (strcmp(callee, "bool") == 0) {
                snprintf(type_out, type_cap, "%s", "bool");
                snprintf(abi_out, abi_cap, "%s", "bool");
            } else {
                snprintf(type_out, type_cap, "%s", strcmp(callee, "cstring") == 0 ? "cstring" : "ptr");
                snprintf(abi_out, abi_cap, "%s", "ptr");
            }
            return true;
        }
        if (v3_resolve_call_target(plan, lowering, current_function, aliases, alias_count, callee, &target)) {
            if (target.return_abi_class[0] == '\0' || strcmp(target.return_abi_class, "void") == 0) {
                return false;
            }
            if (target.return_type[0] != '\0') {
                snprintf(type_out, type_cap, "%s", target.return_type);
            } else {
                v3_type_text_from_abi_class(target.return_abi_class, type_out, type_cap);
            }
            snprintf(abi_out, abi_cap, "%s", target.return_abi_class);
            return type_out[0] != '\0';
        }
        snprintf(ctor_name, sizeof(ctor_name), "%s", ctor_base);
        if (ctor_name[0] >= 'A' && ctor_name[0] <= 'Z') {
            char normalized_ctor[256];
            if (!v3_normalize_type_text(lowering,
                                        current_function->owner_module_path,
                                        aliases,
                                        alias_count,
                                        callee,
                                        normalized_ctor,
                                        sizeof(normalized_ctor))) {
                return false;
            }
            snprintf(type_out, type_cap, "%s", normalized_ctor);
            snprintf(abi_out, abi_cap, "%s", v3_type_abi_class(normalized_ctor));
            return true;
        }
    }
    if (v3_parse_prefix_single_arg_call(expr, callee, sizeof(callee), arg_text, sizeof(arg_text))) {
        V3AsmCallTarget target;
        V3ResultIntrinsicKind result_kind =
            v3_resolve_result_intrinsic_kind(lowering,
                                             current_function->owner_module_path,
                                             aliases,
                                             alias_count,
                                             callee);
        if (result_kind == V3_RESULT_INTRINSIC_IS_ERR ||
            result_kind == V3_RESULT_INTRINSIC_IS_OK) {
            char arg_type[256];
            char arg_abi[32];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    arg_text,
                                    arg_type,
                                    sizeof(arg_type),
                                    arg_abi,
                                    sizeof(arg_abi)) ||
                strcmp(arg_abi, "composite") != 0 ||
                !v3_startswith(arg_type, "Result[")) {
                return false;
            }
            snprintf(type_out, type_cap, "%s", "bool");
            snprintf(abi_out, abi_cap, "%s", "bool");
            return true;
        }
        if (result_kind == V3_RESULT_INTRINSIC_VALUE ||
            result_kind == V3_RESULT_INTRINSIC_ERROR ||
            result_kind == V3_RESULT_INTRINSIC_ERROR_TEXT ||
            result_kind == V3_RESULT_INTRINSIC_ERROR_INFO_OF) {
            char arg_type[256];
            char arg_abi[32];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    arg_text,
                                    arg_type,
                                    sizeof(arg_type),
                                    arg_abi,
                                    sizeof(arg_abi)) ||
                strcmp(arg_abi, "composite") != 0 ||
                !v3_startswith(arg_type, "Result[")) {
                return false;
            }
            if (result_kind == V3_RESULT_INTRINSIC_VALUE) {
                char inner_type[256];
                if (!v3_parse_result_inner_type(arg_type, inner_type, sizeof(inner_type))) {
                    return false;
                }
                snprintf(type_out, type_cap, "%s", inner_type);
                snprintf(abi_out, abi_cap, "%s", v3_type_abi_class(inner_type));
                return true;
            }
            if (result_kind == V3_RESULT_INTRINSIC_ERROR_INFO_OF) {
                snprintf(type_out, type_cap, "%s", "std/result::ErrorInfo");
                snprintf(abi_out, abi_cap, "%s", "composite");
                return true;
            }
            snprintf(type_out, type_cap, "%s", "str");
            snprintf(abi_out, abi_cap, "%s", "composite");
            return true;
        }
        if (result_kind == V3_RESULT_INTRINSIC_OK) {
            char value_type[256];
            char value_abi[32];
            char normalized_value_type[256];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    arg_text,
                                    value_type,
                                    sizeof(value_type),
                                    value_abi,
                                    sizeof(value_abi)) ||
                !v3_normalize_type_text(lowering,
                                        current_function->owner_module_path,
                                        aliases,
                                        alias_count,
                                        value_type,
                                        normalized_value_type,
                                        sizeof(normalized_value_type))) {
                return false;
            }
            snprintf(type_out, type_cap, "Result[%s]", normalized_value_type);
            snprintf(abi_out, abi_cap, "%s", "composite");
            return true;
        }
        if ((result_kind == V3_RESULT_INTRINSIC_ERR ||
             result_kind == V3_RESULT_INTRINSIC_ERR_INFO) &&
            v3_extract_trailing_type_arg_text(callee, type_out, type_cap) &&
            v3_normalize_type_text(lowering,
                                   current_function->owner_module_path,
                                   aliases,
                                   alias_count,
                                   type_out,
                                   type_out,
                                   type_cap)) {
            snprintf(abi_out, abi_cap, "%s", "composite");
            {
                char normalized_inner_type[256];
                snprintf(normalized_inner_type, sizeof(normalized_inner_type), "%s", type_out);
                snprintf(type_out, type_cap, "Result[%s]", normalized_inner_type);
            }
            return true;
        }
        if (strcmp(callee, "len") == 0) {
            char arg_type[256];
            char arg_abi[32];
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    arg_text,
                                    arg_type,
                                    sizeof(arg_type),
                                    arg_abi,
                                    sizeof(arg_abi))) {
                return false;
            }
            if (strcmp(arg_type, "str") != 0 &&
                strcmp(arg_type, "Bytes") != 0 &&
                !(strlen(arg_type) > 2U && strcmp(arg_type + strlen(arg_type) - 2U, "[]") == 0)) {
                return false;
            }
            snprintf(type_out, type_cap, "%s", "int32");
            snprintf(abi_out, abi_cap, "%s", "i32");
            return true;
        }
        if (v3_resolve_call_target(plan, lowering, current_function, aliases, alias_count, callee, &target)) {
            if (target.return_abi_class[0] == '\0' || strcmp(target.return_abi_class, "void") == 0) {
                return false;
            }
            if (target.return_type[0] != '\0') {
                snprintf(type_out, type_cap, "%s", target.return_type);
            } else {
                v3_type_text_from_abi_class(target.return_abi_class, type_out, type_cap);
            }
            snprintf(abi_out, abi_cap, "%s", target.return_abi_class);
            return type_out[0] != '\0';
        }
    }
    return false;
}

static bool v3_codegen_call_scalar(const V3SystemLinkPlanStub *plan,
                                   const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *current_function,
                                   const V3ImportAlias *aliases,
                                   size_t alias_count,
                                   const V3AsmLocalSlot *locals,
                                   size_t local_count,
                                   const char *callee,
                                   char args[][4096],
                                   size_t arg_count,
                                   const char *expected_abi,
                                   int target_reg,
                                   int32_t call_arg_base,
                                   int32_t string_temp_base,
                                   int32_t string_temp_stride,
                                   int32_t *string_label_index_io,
                                   int call_depth,
                                   char *out,
                                   size_t cap) {
    V3AsmCallTarget target;
    size_t i;
    if (!v3_resolve_call_target(plan, lowering, current_function, aliases, alias_count, callee, &target)) {
        fprintf(stderr,
                "[cheng_v3_seed] scalar call resolve failed caller=%s callee=%s\n",
                current_function->symbol_text,
                callee);
        return false;
    }
    if (!v3_abi_class_scalar_or_ptr(target.return_abi_class)) {
        fprintf(stderr,
                "[cheng_v3_seed] scalar call non-scalar return caller=%s callee=%s ret_abi=%s\n",
                current_function->symbol_text,
                callee,
                target.return_abi_class);
        return false;
    }
    if (target.param_count != arg_count || target.param_count > CHENG_V3_MAX_CALL_ARGS) {
        fprintf(stderr,
                "[cheng_v3_seed] scalar call param mismatch caller=%s callee=%s expected=%zu got=%zu\n",
                current_function->symbol_text,
                callee,
                target.param_count,
                arg_count);
        return false;
    }
    for (i = 0; i < arg_count; ++i) {
        if (target.param_is_var[i]) {
            char lvalue_type[256];
            char lvalue_abi[32];
            int32_t local_index = v3_find_local_slot(locals, local_count, args[i]);
            if (local_index >= 0) {
                if (!v3_emit_slot_address(out, cap, &locals[local_index], 15)) {
                    fprintf(stderr,
                            "[cheng_v3_seed] scalar call var-arg address failed caller=%s callee=%s arg_index=%zu local=%s\n",
                            current_function->symbol_text,
                            callee,
                            i,
                            locals[local_index].name);
                    return false;
                }
            } else if (!v3_emit_lvalue_address(plan,
                                               lowering,
                                               current_function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               args[i],
                                               lvalue_type,
                                               sizeof(lvalue_type),
                                               lvalue_abi,
                                               sizeof(lvalue_abi),
                                               15,
                                               call_arg_base,
                                               string_temp_base,
                                               string_temp_stride,
                                               call_depth + 1,
                                               out,
                                               cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] scalar call var-arg lvalue failed caller=%s callee=%s arg_index=%zu expr=%s\n",
                        current_function->symbol_text,
                        callee,
                        i,
                        args[i]);
                return false;
            }
            v3_emit_call_arg_spill_store(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         i,
                                         "ptr",
                                         15);
            continue;
        }
        if (v3_abi_class_scalar_or_ptr(target.param_abi_classes[i])) {
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        current_function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        args[i],
                                        target.param_abi_classes[i],
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        call_depth + 1,
                                        out,
                                        cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] scalar call scalar-arg emit failed caller=%s callee=%s arg_index=%zu expr=%s abi=%s\n",
                        current_function->symbol_text,
                        callee,
                        i,
                        args[i],
                        target.param_abi_classes[i]);
                return false;
            }
            v3_emit_call_arg_spill_store(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         i,
                                         target.param_abi_classes[i],
                                         9);
            continue;
        }
        {
            int32_t local_index = v3_find_local_slot(locals, local_count, args[i]);
            char temp_name[128];
            bool need_materialize = false;
            if (local_index < 0) {
                v3_call_arg_temp_name(current_function,
                                      callee,
                                      i,
                                      args[i],
                                      temp_name,
                                      sizeof(temp_name));
                local_index = v3_find_local_slot(locals, local_count, temp_name);
                need_materialize = true;
                if (local_index < 0) {
                    fprintf(stderr,
                            "[cheng_v3_seed] scalar call composite-arg local missing caller=%s callee=%s arg_index=%zu expr=%s\n",
                            current_function->symbol_text,
                            callee,
                            i,
                            args[i]);
                    return false;
                }
            }
            if (need_materialize &&
                !v3_materialize_composite_expr_into_slot(plan,
                                                         lowering,
                                                         current_function,
                                                         aliases,
                                                         alias_count,
                                                         locals,
                                                         local_count,
                                                         callee,
                                                         i,
                                                         args[i],
                                                         &locals[local_index],
                                                         call_arg_base,
                                                         string_temp_base,
                                                         string_temp_stride,
                                                         string_label_index_io,
                                                         call_depth + 1,
                                                         out,
                                                         cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] scalar call composite-arg materialize failed caller=%s callee=%s arg_index=%zu expr=%s\n",
                        current_function->symbol_text,
                        callee,
                        i,
                        args[i]);
                return false;
            }
            if (!v3_emit_slot_address(out, cap, &locals[local_index], 15)) {
                fprintf(stderr,
                        "[cheng_v3_seed] scalar call composite-arg address failed caller=%s callee=%s arg_index=%zu local=%s\n",
                        current_function->symbol_text,
                        callee,
                        i,
                        locals[local_index].name);
                return false;
            }
            v3_emit_call_arg_spill_store(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         i,
                                         "ptr",
                                         15);
        }
    }
    if (arg_count > CHENG_V3_CALL_ARG_REGS) {
        int32_t stack_arg_bytes =
            v3_align_up_i32((int32_t)(arg_count - CHENG_V3_CALL_ARG_REGS) * 8, 16);
        v3_text_appendf(out, cap, "  sub sp, sp, #%d\n", stack_arg_bytes);
        for (i = CHENG_V3_CALL_ARG_REGS; i < arg_count; ++i) {
            const char *stack_abi =
                v3_abi_class_scalar_or_ptr(target.param_abi_classes[i]) ?
                target.param_abi_classes[i] : "ptr";
            int32_t stack_offset = (int32_t)(i - CHENG_V3_CALL_ARG_REGS) * 8;
            v3_emit_call_arg_spill_load(out,
                                        cap,
                                        call_arg_base,
                                        call_depth,
                                        i,
                                        9,
                                        stack_abi);
            if (strcmp(stack_abi, "i32") == 0 || strcmp(stack_abi, "bool") == 0) {
                v3_text_appendf(out, cap, "  str w9, [sp, #%d]\n", stack_offset);
            } else {
                v3_text_appendf(out, cap, "  str x9, [sp, #%d]\n", stack_offset);
            }
        }
    }
    for (i = 0; i < arg_count && i < CHENG_V3_CALL_ARG_REGS; ++i) {
        v3_emit_call_arg_spill_load(out,
                                    cap,
                                    call_arg_base,
                                    call_depth,
                                    i,
                                    (int)i,
                                    v3_abi_class_scalar_or_ptr(target.param_abi_classes[i]) ?
                                    target.param_abi_classes[i] : "ptr");
    }
    v3_text_appendf(out, cap, "  bl %s\n", target.symbol_name);
    if (arg_count > CHENG_V3_CALL_ARG_REGS) {
        int32_t stack_arg_bytes =
            v3_align_up_i32((int32_t)(arg_count - CHENG_V3_CALL_ARG_REGS) * 8, 16);
        v3_text_appendf(out, cap, "  add sp, sp, #%d\n", stack_arg_bytes);
    }
    if (strcmp(expected_abi, "void") != 0 &&
        strcmp(target.return_abi_class, expected_abi) != 0) {
        if (!(strcmp(expected_abi, "i64") == 0 && strcmp(target.return_abi_class, "i32") == 0) &&
            !(strcmp(expected_abi, "ptr") == 0 && strcmp(target.return_abi_class, "ptr") == 0)) {
            fprintf(stderr,
                    "[cheng_v3_seed] scalar call return abi mismatch caller=%s callee=%s expected=%s got=%s\n",
                    current_function->symbol_text,
                    callee,
                    expected_abi,
                    target.return_abi_class);
            return false;
        }
    }
    if (strcmp(expected_abi, "void") != 0 && target_reg != 0) {
        if (strcmp(expected_abi, "i32") == 0 || strcmp(expected_abi, "bool") == 0) {
            v3_text_appendf(out, cap, "  mov w%d, w0\n", target_reg);
        } else {
            v3_text_appendf(out, cap, "  mov x%d, x0\n", target_reg);
        }
    }
    return true;
}

static bool v3_emit_result_constructor_into_address(const V3SystemLinkPlanStub *plan,
                                                    const V3LoweringPlanStub *lowering,
                                                    const V3LoweredFunctionStub *current_function,
                                                    const V3ImportAlias *aliases,
                                                    size_t alias_count,
                                                    const V3AsmLocalSlot *locals,
                                                    size_t local_count,
                                                    V3ResultIntrinsicKind kind,
                                                    const char *callee,
                                                    char args[][4096],
                                                    size_t arg_count,
                                                    const char *dest_type_text,
                                                    int dest_addr_reg,
                                                    int32_t call_arg_base,
                                                    int32_t string_temp_base,
                                                    int32_t string_temp_stride,
                                                    int32_t *string_label_index_io,
                                                    int call_depth,
                                                    char *out,
                                                    size_t cap) {
    char normalized_dest_type[256];
    char explicit_inner_type[256];
    char normalized_explicit_inner_type[256];
    char result_inner_type[256];
    char ok_type[256];
    char ok_abi[32];
    char value_type[256];
    char value_abi[32];
    char err_type[256];
    char err_abi[32];
    char err_code_type[256];
    char err_code_abi[32];
    char err_msg_type[256];
    char err_msg_abi[32];
    V3TypeLayoutStub result_layout;
    int32_t ok_offset = 0;
    int32_t value_offset = 0;
    int32_t err_offset = 0;
    int32_t err_code_offset = 0;
    int32_t err_msg_offset = 0;
    int value_addr_reg = dest_addr_reg == 14 ? 15 : 14;
    int err_addr_reg = dest_addr_reg == 15 ? 13 : 15;
    int err_msg_addr_reg = err_addr_reg == 14 ? 13 : 14;
    if (!v3_normalize_type_text(lowering,
                                current_function->owner_module_path,
                                aliases,
                                alias_count,
                                dest_type_text,
                                normalized_dest_type,
                                sizeof(normalized_dest_type)) ||
        !v3_startswith(normalized_dest_type, "Result[") ||
        !v3_parse_result_inner_type(normalized_dest_type,
                                    result_inner_type,
                                    sizeof(result_inner_type)) ||
        !v3_compute_type_layout_impl(lowering, normalized_dest_type, &result_layout, 0U) ||
        !v3_resolve_field_meta(lowering,
                               normalized_dest_type,
                               "ok",
                               ok_type,
                               sizeof(ok_type),
                               ok_abi,
                               sizeof(ok_abi),
                               &ok_offset) ||
        !v3_resolve_field_meta(lowering,
                               normalized_dest_type,
                               "value",
                               value_type,
                               sizeof(value_type),
                               value_abi,
                               sizeof(value_abi),
                               &value_offset) ||
        !v3_resolve_field_meta(lowering,
                               normalized_dest_type,
                               "err",
                               err_type,
                               sizeof(err_type),
                               err_abi,
                               sizeof(err_abi),
                               &err_offset)) {
        return false;
    }
    if (v3_extract_trailing_type_arg_text(callee,
                                          explicit_inner_type,
                                          sizeof(explicit_inner_type))) {
        if (!v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    explicit_inner_type,
                                    normalized_explicit_inner_type,
                                    sizeof(normalized_explicit_inner_type)) ||
            strcmp(normalized_explicit_inner_type, result_inner_type) != 0) {
            return false;
        }
    }
    if (!v3_emit_zero_address_range(out, cap, dest_addr_reg, result_layout.size)) {
        return false;
    }
    v3_emit_call_dest_spill_store(out,
                                  cap,
                                  call_arg_base,
                                  call_depth,
                                  dest_addr_reg);
    if (kind == V3_RESULT_INTRINSIC_OK) {
        if (arg_count != 1U || !v3_emit_mov_imm(out, cap, 9, 1)) {
            return false;
        }
        v3_emit_call_dest_spill_load(out,
                                     cap,
                                     call_arg_base,
                                     call_depth,
                                     dest_addr_reg);
        v3_emit_store_scalar_to_address(out, cap, dest_addr_reg, ok_offset, "bool", 9);
        if (v3_abi_class_scalar_or_ptr(value_abi)) {
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        current_function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        args[0],
                                        value_abi,
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        call_depth + 1,
                                        out,
                                        cap)) {
                return false;
            }
            v3_emit_call_dest_spill_load(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         value_addr_reg);
            if (value_offset != 0 &&
                !v3_emit_add_address_offset(out, cap, value_addr_reg, value_addr_reg, value_offset)) {
                return false;
            }
            v3_emit_store_scalar_to_address(out, cap, value_addr_reg, 0, value_abi, 9);
            return true;
        }
        v3_emit_call_dest_spill_load(out,
                                     cap,
                                     call_arg_base,
                                     call_depth,
                                     value_addr_reg);
        if (value_offset != 0 &&
            !v3_emit_add_address_offset(out, cap, value_addr_reg, value_addr_reg, value_offset)) {
            return false;
        }
        return v3_materialize_composite_expr_into_address(plan,
                                                          lowering,
                                                          current_function,
                                                          aliases,
                                                          alias_count,
                                                          locals,
                                                          local_count,
                                                          args[0],
                                                          value_type,
                                                          value_addr_reg,
                                                          call_arg_base,
                                                          string_temp_base,
                                                          string_temp_stride,
                                                          string_label_index_io,
                                                          call_depth + 1,
                                                          out,
                                                          cap);
    }
    v3_emit_call_dest_spill_load(out,
                                 cap,
                                 call_arg_base,
                                 call_depth,
                                 err_addr_reg);
    if (err_offset != 0 &&
        !v3_emit_add_address_offset(out, cap, err_addr_reg, err_addr_reg, err_offset)) {
        return false;
    }
    if (kind == V3_RESULT_INTRINSIC_ERR_INFO) {
        return arg_count == 1U &&
               strcmp(err_abi, "composite") == 0 &&
               v3_materialize_composite_expr_into_address(plan,
                                                          lowering,
                                                          current_function,
                                                          aliases,
                                                          alias_count,
                                                          locals,
                                                          local_count,
                                                          args[0],
                                                          err_type,
                                                          err_addr_reg,
                                                          call_arg_base,
                                                          string_temp_base,
                                                          string_temp_stride,
                                                          string_label_index_io,
                                                          call_depth + 1,
                                                          out,
                                                          cap);
    }
    if (!v3_resolve_field_meta(lowering,
                               err_type,
                               "code",
                               err_code_type,
                               sizeof(err_code_type),
                               err_code_abi,
                               sizeof(err_code_abi),
                               &err_code_offset) ||
        !v3_resolve_field_meta(lowering,
                               err_type,
                               "msg",
                               err_msg_type,
                               sizeof(err_msg_type),
                               err_msg_abi,
                               sizeof(err_msg_abi),
                               &err_msg_offset)) {
        return false;
    }
    if (kind == V3_RESULT_INTRINSIC_ERR) {
        if (arg_count != 1U || !v3_emit_mov_imm(out, cap, 9, 1)) {
            return false;
        }
        v3_emit_store_scalar_to_address(out, cap, err_addr_reg, err_code_offset, "i32", 9);
        v3_emit_call_dest_spill_load(out,
                                     cap,
                                     call_arg_base,
                                     call_depth,
                                     err_msg_addr_reg);
        if (err_offset != 0 &&
            !v3_emit_add_address_offset(out, cap, err_msg_addr_reg, err_msg_addr_reg, err_offset)) {
            return false;
        }
        if (err_msg_offset != 0 &&
            !v3_emit_add_address_offset(out, cap, err_msg_addr_reg, err_msg_addr_reg, err_msg_offset)) {
            return false;
        }
        return strcmp(err_msg_abi, "composite") == 0 &&
               v3_materialize_composite_expr_into_address(plan,
                                                          lowering,
                                                          current_function,
                                                          aliases,
                                                          alias_count,
                                                          locals,
                                                          local_count,
                                                          args[0],
                                                          err_msg_type,
                                                          err_msg_addr_reg,
                                                          call_arg_base,
                                                          string_temp_base,
                                                          string_temp_stride,
                                                          string_label_index_io,
                                                          call_depth + 1,
                                                          out,
                                                          cap);
    }
    if (kind == V3_RESULT_INTRINSIC_ERR_CODE) {
        if (arg_count != 2U || strcmp(err_msg_abi, "composite") != 0) {
            return false;
        }
        if (!v3_codegen_expr_scalar(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    args[0],
                                    "i32",
                                    9,
                                    call_arg_base,
                                    string_temp_base,
                                    string_temp_stride,
                                    call_depth + 1,
                                        out,
                                        cap)) {
            return false;
        }
        v3_emit_store_scalar_to_address(out, cap, err_addr_reg, err_code_offset, "i32", 9);
        v3_emit_call_dest_spill_load(out,
                                     cap,
                                     call_arg_base,
                                     call_depth,
                                     err_msg_addr_reg);
        if (err_offset != 0 &&
            !v3_emit_add_address_offset(out, cap, err_msg_addr_reg, err_msg_addr_reg, err_offset)) {
            return false;
        }
        if (err_msg_offset != 0 &&
            !v3_emit_add_address_offset(out, cap, err_msg_addr_reg, err_msg_addr_reg, err_msg_offset)) {
            return false;
        }
        return v3_materialize_composite_expr_into_address(plan,
                                                          lowering,
                                                          current_function,
                                                          aliases,
                                                          alias_count,
                                                          locals,
                                                          local_count,
                                                          args[1],
                                                          err_msg_type,
                                                          err_msg_addr_reg,
                                                          call_arg_base,
                                                          string_temp_base,
                                                          string_temp_stride,
                                                          string_label_index_io,
                                                          call_depth + 1,
                                                          out,
                                                          cap);
    }
    return false;
}

static bool v3_codegen_binary_expr_scalar(const V3SystemLinkPlanStub *plan,
                                          const V3LoweringPlanStub *lowering,
                                          const V3LoweredFunctionStub *current_function,
                                          const V3ImportAlias *aliases,
                                          size_t alias_count,
                                          const V3AsmLocalSlot *locals,
                                          size_t local_count,
                                          const char *expr_text,
                                          const char *op,
                                          const char *expected_abi,
                                          int target_reg,
                                          int32_t call_arg_base,
                                          int32_t string_temp_base,
                                          int32_t string_temp_stride,
                                          int call_depth,
                                          char *out,
                                          size_t cap) {
    int32_t op_index = v3_find_top_level_binary_op_last(expr_text, op);
    char left[4096];
    char right[4096];
    if (op_index < 0) {
        return false;
    }
    snprintf(left, sizeof(left), "%.*s", op_index, expr_text);
    snprintf(right, sizeof(right), "%s", expr_text + op_index + strlen(op));
    v3_trim_copy_text(left, left, sizeof(left));
    v3_trim_copy_text(right, right, sizeof(right));
    if (!v3_codegen_expr_scalar(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                left,
                                expected_abi,
                                target_reg,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                call_depth,
                                out,
                                cap)) {
        return false;
    }
    if (!v3_codegen_expr_scalar(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                right,
                                expected_abi,
                                target_reg + 1,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                call_depth,
                                out,
                                cap)) {
        return false;
    }
    if (strcmp(op, "|") == 0) {
        v3_text_appendf(out, cap, "  orr x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "||") == 0) {
        v3_text_appendf(out, cap, "  orr w%d, w%d, w%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "^") == 0) {
        v3_text_appendf(out, cap, "  eor x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "&") == 0) {
        v3_text_appendf(out, cap, "  and x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "&&") == 0) {
        v3_text_appendf(out, cap, "  and w%d, w%d, w%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "+") == 0) {
        v3_text_appendf(out, cap, "  add x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "-") == 0) {
        v3_text_appendf(out, cap, "  sub x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "*") == 0) {
        v3_text_appendf(out, cap, "  mul x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "/") == 0) {
        v3_text_appendf(out, cap, "  sdiv x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, "%") == 0) {
        v3_text_appendf(out, cap,
                        "  sdiv x14, x%d, x%d\n"
                        "  msub x%d, x14, x%d, x%d\n",
                        target_reg,
                        target_reg + 1,
                        target_reg,
                        target_reg + 1,
                        target_reg);
    } else if (strcmp(op, "<<") == 0) {
        v3_text_appendf(out, cap, "  lslv x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else if (strcmp(op, ">>") == 0) {
        v3_text_appendf(out, cap, "  lsrv x%d, x%d, x%d\n", target_reg, target_reg, target_reg + 1);
    } else {
        return false;
    }
    return true;
}

static bool v3_codegen_compare_expr_scalar(const V3SystemLinkPlanStub *plan,
                                           const V3LoweringPlanStub *lowering,
                                           const V3LoweredFunctionStub *current_function,
                                           const V3ImportAlias *aliases,
                                           size_t alias_count,
                                           const V3AsmLocalSlot *locals,
                                           size_t local_count,
                                           const char *expr_text,
                                           const char *op,
                                           int target_reg,
                                           int32_t call_arg_base,
                                           int32_t string_temp_base,
                                           int32_t string_temp_stride,
                                           int call_depth,
                                           char *out,
                                           size_t cap) {
    int32_t op_index = v3_find_top_level_binary_op_last(expr_text, op);
    char left[4096];
    char right[4096];
    const char *cond = NULL;
    if (op_index < 0) {
        return false;
    }
    snprintf(left, sizeof(left), "%.*s", op_index, expr_text);
    snprintf(right, sizeof(right), "%s", expr_text + op_index + strlen(op));
    v3_trim_copy_text(left, left, sizeof(left));
    v3_trim_copy_text(right, right, sizeof(right));
    if (!v3_codegen_expr_scalar(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                left,
                                "i64",
                                target_reg,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                call_depth,
                                out,
                                cap) ||
        !v3_codegen_expr_scalar(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                right,
                                "i64",
                                target_reg + 1,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                call_depth,
                                out,
                                cap)) {
        return false;
    }
    if (strcmp(op, "==") == 0) {
        cond = "eq";
    } else if (strcmp(op, "!=") == 0) {
        cond = "ne";
    } else if (strcmp(op, "<") == 0) {
        cond = "lt";
    } else if (strcmp(op, "<=") == 0) {
        cond = "le";
    } else if (strcmp(op, ">") == 0) {
        cond = "gt";
    } else if (strcmp(op, ">=") == 0) {
        cond = "ge";
    } else {
        return false;
    }
    v3_text_appendf(out, cap,
                    "  cmp x%d, x%d\n"
                    "  cset w%d, %s\n",
                    target_reg,
                    target_reg + 1,
                    target_reg,
                    cond);
    return true;
}

static bool v3_parse_result_inner_type(const char *result_type,
                                       char *inner_type_out,
                                       size_t inner_type_cap) {
    size_t len;
    if (!v3_startswith(result_type, "Result[")) {
        return false;
    }
    len = strlen(result_type);
    if (len < 9U || result_type[len - 1U] != ']') {
        return false;
    }
    snprintf(inner_type_out, inner_type_cap, "%.*s", (int)(len - 8U), result_type + 7);
    return inner_type_out[0] != '\0';
}

static bool v3_emit_result_field_address(const V3SystemLinkPlanStub *plan,
                                         const V3LoweringPlanStub *lowering,
                                         const V3LoweredFunctionStub *current_function,
                                         const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         const V3AsmLocalSlot *locals,
                                         size_t local_count,
                                         const char *arg_expr,
                                         const char *field_name,
                                         int addr_reg,
                                         char *out,
                                         size_t cap,
                                         char *field_type_out,
                                         size_t field_type_cap,
                                         char *field_abi_out,
                                         size_t field_abi_cap) {
    char arg_type[256];
    char arg_abi[32];
    char trimmed[4096];
    int32_t field_offset = 0;
    int32_t local_index;
    v3_trim_copy_text(arg_expr, trimmed, sizeof(trimmed));
    if (!v3_infer_expr_type(plan,
                            lowering,
                            current_function,
                            aliases,
                            alias_count,
                            locals,
                            local_count,
                            trimmed,
                            arg_type,
                            sizeof(arg_type),
                            arg_abi,
                            sizeof(arg_abi)) ||
        strcmp(arg_abi, "composite") != 0 ||
        !v3_startswith(arg_type, "Result[")) {
        return false;
    }
    local_index = v3_find_local_slot(locals, local_count, trimmed);
    if (local_index >= 0) {
        if (!v3_emit_slot_address(out, cap, &locals[local_index], addr_reg)) {
            return false;
        }
    } else if (!v3_emit_member_access_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              trimmed,
                                              arg_type,
                                              sizeof(arg_type),
                                              arg_abi,
                                              sizeof(arg_abi),
                                              addr_reg,
                                              0,
                                              0,
                                              0,
                                              0,
                                              out,
                                              cap) &&
               !v3_emit_field_path_address(out,
                                           cap,
                                           locals,
                                           local_count,
                                           lowering,
                                           trimmed,
                                           arg_type,
                                           sizeof(arg_type),
                                           arg_abi,
                                           sizeof(arg_abi),
                                           addr_reg)) {
        return false;
    }
    if (!v3_resolve_field_meta(lowering,
                               arg_type,
                               field_name,
                               field_type_out,
                               field_type_cap,
                               field_abi_out,
                               field_abi_cap,
                               &field_offset)) {
        return false;
    }
    if (field_offset != 0 &&
        !v3_emit_add_address_offset(out, cap, addr_reg, addr_reg, field_offset)) {
        return false;
    }
    return true;
}

static bool v3_codegen_intrinsic_len_expr(const V3SystemLinkPlanStub *plan,
                                          const V3LoweringPlanStub *lowering,
                                          const V3LoweredFunctionStub *current_function,
                                          const V3ImportAlias *aliases,
                                          size_t alias_count,
                                          const V3AsmLocalSlot *locals,
                                          size_t local_count,
                                          const char *arg_expr,
                                          int target_reg,
                                          char *out,
                                          size_t cap) {
    char trimmed[4096];
    char field_type[256];
    char field_abi[32];
    char arg_type[256];
    char arg_abi[32];
    int32_t local_index;
    v3_trim_copy_text(arg_expr, trimmed, sizeof(trimmed));
    if (v3_parse_string_literal_text(trimmed, field_type, sizeof(field_type))) {
        return v3_emit_mov_imm(out, cap, target_reg, (int64_t)strlen(field_type));
    }
    if (strcmp(trimmed, "[]") == 0) {
        return v3_emit_mov_imm(out, cap, target_reg, 0);
    }
    if (v3_emit_member_access_address(plan,
                                      lowering,
                                      current_function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      trimmed,
                                      field_type,
                                      sizeof(field_type),
                                      field_abi,
                                      sizeof(field_abi),
                                      15,
                                      0,
                                      0,
                                      0,
                                      0,
                                      out,
                                      cap) ||
        v3_emit_field_path_address(out,
                                   cap,
                                   locals,
                                   local_count,
                                   lowering,
                                   trimmed,
                                   field_type,
                                   sizeof(field_type),
                                   field_abi,
                                   sizeof(field_abi),
                                   15)) {
        int32_t offset = 0;
        if (strcmp(field_type, "str") == 0 || strcmp(field_type, "Bytes") == 0) {
            offset = 8;
        } else if (strlen(field_type) > 2U && strcmp(field_type + strlen(field_type) - 2U, "[]") == 0) {
            offset = 0;
        } else {
            return false;
        }
        v3_text_appendf(out, cap, "  ldrsw x%d, [x15, #%d]\n", target_reg, offset);
        return true;
    }
    local_index = v3_find_local_slot(locals, local_count, trimmed);
    if (local_index >= 0) {
        int32_t offset = 0;
        if (strcmp(locals[local_index].type_text, "str") == 0 || strcmp(locals[local_index].type_text, "Bytes") == 0) {
            offset = 8;
        } else if (strlen(locals[local_index].type_text) > 2U &&
                   strcmp(locals[local_index].type_text + strlen(locals[local_index].type_text) - 2U, "[]") == 0) {
            offset = 0;
        } else {
            return false;
        }
        if (!v3_emit_slot_address(out, cap, &locals[local_index], 15)) {
            return false;
        }
        v3_text_appendf(out, cap, "  ldrsw x%d, [x15, #%d]\n", target_reg, offset);
        return true;
    }
    if (!v3_infer_expr_type(plan,
                            lowering,
                            current_function,
                            aliases,
                            alias_count,
                            locals,
                            local_count,
                            trimmed,
                            arg_type,
                            sizeof(arg_type),
                            arg_abi,
                            sizeof(arg_abi))) {
        return false;
    }
    if (strcmp(arg_type, "str") != 0 &&
        strcmp(arg_type, "Bytes") != 0 &&
        !(strlen(arg_type) > 2U && strcmp(arg_type + strlen(arg_type) - 2U, "[]") == 0)) {
        return false;
    }
    return false;
}

static bool v3_codegen_expr_scalar(const V3SystemLinkPlanStub *plan,
                                   const V3LoweringPlanStub *lowering,
                                   const V3LoweredFunctionStub *current_function,
                                   const V3ImportAlias *aliases,
                                   size_t alias_count,
                                   const V3AsmLocalSlot *locals,
                                   size_t local_count,
                                   const char *expr_text,
                                   const char *expected_abi,
                                   int target_reg,
                                   int32_t call_arg_base,
                                   int32_t string_temp_base,
                                   int32_t string_temp_stride,
                                   int call_depth,
                                   char *out,
                                   size_t cap) {
    char expr[8192];
    char callee[PATH_MAX];
    char arg_text[4096];
    char outer[8192];
    char base_expr[4096];
    char index_expr[4096];
    char ternary_cond[4096];
    char ternary_true[4096];
    char ternary_false[4096];
    char args[CHENG_V3_MAX_CALL_ARGS][4096];
    size_t arg_count = 0U;
    int32_t local_index;
    int32_t char_value = 0;
    int64_t literal_value;
    if (target_reg >= 15) {
        return false;
    }
    if (!v3_abi_class_scalar_or_ptr(expected_abi)) {
        return false;
    }
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return false;
    }
    if (v3_trim_outer_parens(expr, outer, sizeof(outer))) {
        return v3_codegen_expr_scalar(plan,
                                      lowering,
                                      current_function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      outer,
                                      expected_abi,
                                      target_reg,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      call_depth,
                                      out,
                                      cap);
    }
    if (v3_parse_ternary_expr(expr,
                              ternary_cond,
                              sizeof(ternary_cond),
                              ternary_true,
                              sizeof(ternary_true),
                              ternary_false,
                              sizeof(ternary_false))) {
        int32_t label_id = v3_next_expr_label_id();
        char false_label[128];
        char end_label[128];
        if (!v3_codegen_expr_scalar(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    ternary_cond,
                                    "bool",
                                    target_reg,
                                    call_arg_base,
                                    string_temp_base,
                                    string_temp_stride,
                                    call_depth + 1,
                                    out,
                                    cap)) {
            return false;
        }
        snprintf(false_label, sizeof(false_label), "L_v3_expr_false_%d", label_id);
        snprintf(end_label, sizeof(end_label), "L_v3_expr_end_%d", label_id);
        v3_text_appendf(out, cap,
                        "  cmp w%d, #0\n"
                        "  b.eq %s\n",
                        target_reg,
                        false_label);
        if (!v3_codegen_expr_scalar(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    ternary_true,
                                    expected_abi,
                                    target_reg,
                                    call_arg_base,
                                    string_temp_base,
                                    string_temp_stride,
                                    call_depth + 1,
                                    out,
                                    cap)) {
            return false;
        }
        v3_text_appendf(out, cap,
                        "  b %s\n"
                        "%s:\n",
                        end_label,
                        false_label);
        if (!v3_codegen_expr_scalar(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    ternary_false,
                                    expected_abi,
                                    target_reg,
                                    call_arg_base,
                                    string_temp_base,
                                    string_temp_stride,
                                    call_depth + 1,
                                    out,
                                    cap)) {
            return false;
        }
        v3_text_appendf(out, cap, "%s:\n", end_label);
        return true;
    }
    if (v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "||", "bool", target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap)) {
        v3_text_appendf(out, cap, "  cmp w%d, #0\n  cset w%d, ne\n", target_reg, target_reg);
        return true;
    }
    if (v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "&&", "bool", target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap)) {
        v3_text_appendf(out, cap, "  cmp w%d, #0\n  cset w%d, ne\n", target_reg, target_reg);
        return true;
    }
    if (v3_codegen_compare_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                       locals, local_count, expr, "==", target_reg,
                                       call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                       out, cap) ||
        v3_codegen_compare_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                       locals, local_count, expr, "!=", target_reg,
                                       call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                       out, cap) ||
        v3_codegen_compare_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                       locals, local_count, expr, "<=", target_reg,
                                       call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                       out, cap) ||
        v3_codegen_compare_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                       locals, local_count, expr, ">=", target_reg,
                                       call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                       out, cap) ||
        v3_codegen_compare_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                       locals, local_count, expr, "<", target_reg,
                                       call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                       out, cap) ||
        v3_codegen_compare_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                       locals, local_count, expr, ">", target_reg,
                                       call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                       out, cap)) {
        return true;
    }
    if (v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "|", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "^", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "&", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "+", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "-", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "*", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "/", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "%", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, "<<", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap) ||
        v3_codegen_binary_expr_scalar(plan, lowering, current_function, aliases, alias_count,
                                      locals, local_count, expr, ">>", expected_abi, target_reg,
                                      call_arg_base, string_temp_base, string_temp_stride, call_depth,
                                      out, cap)) {
        return true;
    }
    if (expr[0] == '!') {
        if (!v3_codegen_expr_scalar(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    expr + 1,
                                    "bool",
                                    target_reg,
                                    call_arg_base,
                                    string_temp_base,
                                    string_temp_stride,
                                    call_depth,
                                    out,
                                    cap)) {
            return false;
        }
        v3_text_appendf(out, cap, "  cmp w%d, #0\n  cset w%d, eq\n", target_reg, target_reg);
        return true;
    }
    if (v3_parse_index_access_expr(expr, base_expr, sizeof(base_expr), index_expr, sizeof(index_expr))) {
        char field_type[256];
        char field_abi[32];
        char base_type[256];
        char base_abi[32];
        if (!v3_emit_index_access_address(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          expr,
                                          field_type,
                                          sizeof(field_type),
                                          field_abi,
                                          sizeof(field_abi),
                                          15,
                                          call_arg_base,
                                          string_temp_base,
                                          string_temp_stride,
                                          call_depth,
                                          out,
                                          cap) ||
            !v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                base_expr,
                                base_type,
                                sizeof(base_type),
                                base_abi,
                                sizeof(base_abi)) ||
            !v3_abi_class_scalar_or_ptr(field_abi)) {
            return false;
        }
        if (strcmp(base_type, "Bytes") == 0 || strcmp(base_type, "str") == 0 || strcmp(field_type, "char") == 0) {
            v3_text_appendf(out, cap, "  ldrb w%d, [x15, #0]\n", target_reg);
        } else if (strcmp(field_abi, "i32") == 0) {
            v3_text_appendf(out, cap, "  ldrsw x%d, [x15, #0]\n", target_reg);
        } else if (strcmp(field_abi, "bool") == 0) {
            v3_text_appendf(out, cap, "  ldrb w%d, [x15, #0]\n", target_reg);
        } else {
            v3_text_appendf(out, cap, "  ldr x%d, [x15, #0]\n", target_reg);
        }
        return true;
    }
    {
        char field_type[256];
        char field_abi[32];
        if (v3_emit_member_access_address(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          expr,
                                          field_type,
                                          sizeof(field_type),
                                          field_abi,
                                          sizeof(field_abi),
                                          15,
                                          call_arg_base,
                                          string_temp_base,
                                          string_temp_stride,
                                          call_depth,
                                          out,
                                          cap) ||
            v3_emit_field_path_address(out,
                                       cap,
                                       locals,
                                       local_count,
                                       lowering,
                                       expr,
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       15)) {
            if (!v3_abi_class_scalar_or_ptr(field_abi)) {
                return false;
            }
            if (strcmp(field_abi, "i32") == 0) {
                v3_text_appendf(out, cap, "  ldrsw x%d, [x15, #0]\n", target_reg);
            } else if (strcmp(field_abi, "bool") == 0) {
                v3_text_appendf(out, cap, "  ldr w%d, [x15, #0]\n", target_reg);
            } else {
                v3_text_appendf(out, cap, "  ldr x%d, [x15, #0]\n", target_reg);
            }
            return true;
        }
    }
    if (v3_parse_call_text(expr, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
        V3ResultIntrinsicKind result_kind =
            v3_resolve_result_intrinsic_kind(lowering,
                                             current_function->owner_module_path,
                                             aliases,
                                             alias_count,
                                             callee);
        if ((result_kind == V3_RESULT_INTRINSIC_IS_ERR ||
             result_kind == V3_RESULT_INTRINSIC_IS_OK) && arg_count == 1U) {
            char field_type[256];
            char field_abi[32];
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              args[0],
                                              "ok",
                                              15,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi))) {
                return false;
            }
            v3_text_appendf(out, cap, "  ldr w%d, [x15, #0]\n", target_reg);
            if (result_kind == V3_RESULT_INTRINSIC_IS_ERR) {
                v3_text_appendf(out, cap, "  cmp w%d, #0\n  cset w%d, eq\n", target_reg, target_reg);
            }
            return true;
        }
        if (result_kind == V3_RESULT_INTRINSIC_VALUE && arg_count == 1U) {
            char field_type[256];
            char field_abi[32];
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              args[0],
                                              "value",
                                              15,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi)) ||
                !v3_abi_class_scalar_or_ptr(field_abi)) {
                return false;
            }
            if (strcmp(field_abi, "i32") == 0) {
                v3_text_appendf(out, cap, "  ldrsw x%d, [x15, #0]\n", target_reg);
            } else if (strcmp(field_abi, "bool") == 0) {
                v3_text_appendf(out, cap, "  ldr w%d, [x15, #0]\n", target_reg);
            } else {
                v3_text_appendf(out, cap, "  ldr x%d, [x15, #0]\n", target_reg);
            }
            return true;
        }
        if (strcmp(callee, "len") == 0 && arg_count == 1U) {
            return v3_codegen_intrinsic_len_expr(plan,
                                                 lowering,
                                                 current_function,
                                                 aliases,
                                                 alias_count,
                                                 locals,
                                                 local_count,
                                                 args[0],
                                                 target_reg,
                                                 out,
                                                 cap);
        }
        if ((strcmp(callee, "int64") == 0 ||
             strcmp(callee, "int32") == 0 ||
             strcmp(callee, "ptr") == 0 ||
             strcmp(callee, "cstring") == 0 ||
             strcmp(callee, "bool") == 0) && arg_count == 1U) {
            return v3_codegen_expr_scalar(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          args[0],
                                          strcmp(callee, "bool") == 0 ? "bool" :
                                          ((strcmp(callee, "int32") == 0) ? "i32" :
                                           ((strcmp(callee, "int64") == 0) ? "i64" : "ptr")),
                                          target_reg,
                                          call_arg_base,
                                          string_temp_base,
                                          string_temp_stride,
                                          call_depth,
                                          out,
                                          cap);
        }
        return v3_codegen_call_scalar(plan,
                                      lowering,
                                      current_function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      callee,
                                      args,
                                      arg_count,
                                      expected_abi,
                                      target_reg,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      NULL,
                                      call_depth,
                                      out,
                                      cap);
    }
    if (v3_parse_prefix_single_arg_call(expr, callee, sizeof(callee), arg_text, sizeof(arg_text))) {
        V3ResultIntrinsicKind result_kind =
            v3_resolve_result_intrinsic_kind(lowering,
                                             current_function->owner_module_path,
                                             aliases,
                                             alias_count,
                                             callee);
        if (result_kind == V3_RESULT_INTRINSIC_IS_ERR ||
            result_kind == V3_RESULT_INTRINSIC_IS_OK) {
            char field_type[256];
            char field_abi[32];
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              arg_text,
                                              "ok",
                                              15,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi))) {
                return false;
            }
            v3_text_appendf(out, cap, "  ldr w%d, [x15, #0]\n", target_reg);
            if (result_kind == V3_RESULT_INTRINSIC_IS_ERR) {
                v3_text_appendf(out, cap, "  cmp w%d, #0\n  cset w%d, eq\n", target_reg, target_reg);
            }
            return true;
        }
        if (result_kind == V3_RESULT_INTRINSIC_VALUE) {
            char field_type[256];
            char field_abi[32];
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              arg_text,
                                              "value",
                                              15,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi)) ||
                !v3_abi_class_scalar_or_ptr(field_abi)) {
                return false;
            }
            if (strcmp(field_abi, "i32") == 0) {
                v3_text_appendf(out, cap, "  ldrsw x%d, [x15, #0]\n", target_reg);
            } else if (strcmp(field_abi, "bool") == 0) {
                v3_text_appendf(out, cap, "  ldr w%d, [x15, #0]\n", target_reg);
            } else {
                v3_text_appendf(out, cap, "  ldr x%d, [x15, #0]\n", target_reg);
            }
            return true;
        }
        if (strcmp(callee, "len") == 0) {
            return v3_codegen_intrinsic_len_expr(plan,
                                                 lowering,
                                                 current_function,
                                                 aliases,
                                                 alias_count,
                                                 locals,
                                                 local_count,
                                                 arg_text,
                                                 target_reg,
                                                 out,
                                                 cap);
        }
        snprintf(args[0], sizeof(args[0]), "%s", arg_text);
        return v3_codegen_call_scalar(plan,
                                      lowering,
                                      current_function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      callee,
                                      args,
                                      1U,
                                      expected_abi,
                                      target_reg,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      NULL,
                                      call_depth,
                                      out,
                                      cap);
    }
    local_index = v3_find_local_slot(locals, local_count, expr);
    if (local_index >= 0) {
        v3_emit_load_slot(out, cap, &locals[local_index], target_reg);
        return true;
    }
    {
        const V3TopLevelConstStub *top_level_const =
            v3_find_expr_top_level_const(lowering,
                                         current_function->owner_module_path,
                                         aliases,
                                         alias_count,
                                         expr);
        if (top_level_const) {
            if (strcmp(top_level_const->abi_class, "bool") == 0) {
                return v3_emit_mov_imm(out, cap, target_reg, top_level_const->bool_value ? 1 : 0);
            }
            if (strcmp(top_level_const->abi_class, "i32") == 0 ||
                strcmp(top_level_const->abi_class, "i64") == 0) {
                return v3_emit_mov_imm(out, cap, target_reg, top_level_const->i64_value);
            }
            return false;
        }
    }
    if (v3_parse_char_literal_text(expr, &char_value)) {
        return v3_emit_mov_imm(out, cap, target_reg, char_value);
    }
    if (v3_parse_int_literal_text(expr, &literal_value)) {
        return v3_emit_mov_imm(out, cap, target_reg, literal_value);
    }
    if (strcmp(expr, "true") == 0) {
        return v3_emit_mov_imm(out, cap, target_reg, 1);
    }
    if (strcmp(expr, "false") == 0 || strcmp(expr, "nil") == 0) {
        return v3_emit_mov_imm(out, cap, target_reg, 0);
    }
    return false;
}

static bool v3_emit_slot_address(char *out,
                                 size_t cap,
                                 const V3AsmLocalSlot *slot,
                                 int reg_index) {
    if (slot->stack_offset < 0) {
        return false;
    }
    if (slot->indirect_value) {
        if (!v3_emit_sp_load_reg(out, cap, slot->stack_offset, "ptr", reg_index)) {
            return false;
        }
        return true;
    }
    return v3_emit_sp_offset_address(out, cap, reg_index, slot->stack_offset);
}

static bool v3_parse_named_arg_meta(const char *arg_text,
                                    char *field_out,
                                    size_t field_cap,
                                    char *expr_out,
                                    size_t expr_cap) {
    bool in_string = false;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    size_t i;
    char trimmed[4096];
    v3_trim_copy_text(arg_text, trimmed, sizeof(trimmed));
    for (i = 0; trimmed[i] != '\0'; ++i) {
        char ch = trimmed[i];
        if (ch == '"' && (i == 0 || trimmed[i - 1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            paren_depth += 1;
            continue;
        }
        if (ch == ')') {
            paren_depth -= 1;
            continue;
        }
        if (ch == '[') {
            bracket_depth += 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth -= 1;
            continue;
        }
        if (paren_depth == 0 && bracket_depth == 0 && ch == ':') {
            snprintf(field_out, field_cap, "%.*s", (int)i, trimmed);
            v3_trim_copy_text(field_out, field_out, field_cap);
            snprintf(expr_out, expr_cap, "%s", trimmed + i + 1);
            v3_trim_copy_text(expr_out, expr_out, expr_cap);
            return field_out[0] != '\0' && expr_out[0] != '\0';
        }
    }
    return false;
}

static bool v3_emit_constructor_into_address(const V3SystemLinkPlanStub *plan,
                                             const V3LoweringPlanStub *lowering,
                                             const V3LoweredFunctionStub *current_function,
                                             const V3ImportAlias *aliases,
                                             size_t alias_count,
                                             const V3AsmLocalSlot *locals,
                                             size_t local_count,
                                             const char *callee,
                                             char args[][4096],
                                             size_t arg_count,
                                             const char *dest_type_text,
                                             int dest_addr_reg,
                                             int32_t call_arg_base,
                                             int32_t string_temp_base,
                                             int32_t string_temp_stride,
                                             int32_t *string_label_index_io,
                                             int call_depth,
                                             char *out,
                                             size_t cap);

static bool v3_codegen_call_into_address(const V3SystemLinkPlanStub *plan,
                                         const V3LoweringPlanStub *lowering,
                                         const V3LoweredFunctionStub *current_function,
                                         const V3ImportAlias *aliases,
                                         size_t alias_count,
                                         const V3AsmLocalSlot *locals,
                                         size_t local_count,
                                         const char *callee,
                                         char args[][4096],
                                         size_t arg_count,
                                         int dest_addr_reg,
                                         int32_t call_arg_base,
                                         int32_t string_temp_base,
                                         int32_t string_temp_stride,
                                         int32_t *string_label_index_io,
                                         int call_depth,
                                         char *out,
                                         size_t cap) {
    V3AsmCallTarget target;
    size_t i;
    int32_t string_temp_index = 0;
    if (!v3_resolve_call_target(plan, lowering, current_function, aliases, alias_count, callee, &target)) {
        return false;
    }
    if (strcmp(target.return_abi_class, "composite") != 0 ||
        target.param_count != arg_count ||
        target.param_count > CHENG_V3_MAX_CALL_ARGS) {
        return false;
    }
    v3_emit_call_dest_spill_store(out,
                                  cap,
                                  call_arg_base,
                                  call_depth,
                                  dest_addr_reg);
    for (i = 0; i < arg_count; ++i) {
        if (target.param_is_var[i]) {
            char lvalue_type[256];
            char lvalue_abi[32];
            int32_t local_index = v3_find_local_slot(locals, local_count, args[i]);
            if (local_index >= 0) {
                if (!v3_emit_slot_address(out, cap, &locals[local_index], 15)) {
                    return false;
                }
            } else if (!v3_emit_lvalue_address(plan,
                                               lowering,
                                               current_function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               args[i],
                                               lvalue_type,
                                               sizeof(lvalue_type),
                                               lvalue_abi,
                                               sizeof(lvalue_abi),
                                               15,
                                               call_arg_base,
                                               string_temp_base,
                                               string_temp_stride,
                                               call_depth + 1,
                                               out,
                                               cap)) {
                return false;
            }
            v3_emit_call_arg_spill_store(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         i,
                                         "ptr",
                                         15);
            continue;
        }
        if (strcmp(target.param_types[i], "str") == 0) {
            char literal[4096];
            int32_t scratch_offset;
            if (string_label_index_io &&
                v3_parse_string_literal_text(args[i], literal, sizeof(literal))) {
                scratch_offset = string_temp_base + call_depth * string_temp_stride + string_temp_index * 24;
                string_temp_index += 1;
                {
                    char scratch_name[64];
                    V3AsmLocalSlot scratch_slot;
                    snprintf(scratch_name, sizeof(scratch_name), "__v3_strtmp_%d_%zu", call_depth, i);
                    memset(&scratch_slot, 0, sizeof(scratch_slot));
                    snprintf(scratch_slot.name, sizeof(scratch_slot.name), "%s", scratch_name);
                    snprintf(scratch_slot.type_text, sizeof(scratch_slot.type_text), "%s", "str");
                    snprintf(scratch_slot.abi_class, sizeof(scratch_slot.abi_class), "%s", "composite");
                    scratch_slot.stack_offset = scratch_offset;
                    scratch_slot.stack_size = 24;
                    scratch_slot.stack_align = 8;
                    if (!v3_emit_str_literal_into_slot(current_function,
                                                       &scratch_slot,
                                                       literal,
                                                       string_label_index_io,
                                                       out,
                                                       cap) ||
                        !v3_emit_slot_address(out, cap, &scratch_slot, 15)) {
                        return false;
                    }
                    v3_emit_call_arg_spill_store(out,
                                                 cap,
                                                 call_arg_base,
                                                 call_depth,
                                                 i,
                                                 "ptr",
                                                 15);
                }
                continue;
            }
        }
        if (v3_abi_class_scalar_or_ptr(target.param_abi_classes[i])) {
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        current_function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        args[i],
                                        target.param_abi_classes[i],
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        call_depth + 1,
                                        out,
                                        cap)) {
                return false;
            }
            v3_emit_call_arg_spill_store(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         i,
                                         target.param_abi_classes[i],
                                         9);
            continue;
        }
        {
            int32_t local_index = v3_find_local_slot(locals, local_count, args[i]);
            char temp_name[128];
            bool need_materialize = false;
            if (local_index < 0) {
                v3_call_arg_temp_name(current_function,
                                      callee,
                                      i,
                                      args[i],
                                      temp_name,
                                      sizeof(temp_name));
                local_index = v3_find_local_slot(locals, local_count, temp_name);
                need_materialize = true;
            }
            if (local_index < 0) {
                fprintf(stderr,
                        "[cheng_v3_seed] composite call-into-address arg local missing caller=%s callee=%s arg_index=%zu expr=%s\n",
                        current_function->symbol_text,
                        callee,
                        i,
                        args[i]);
                return false;
            }
            if (need_materialize &&
                !v3_materialize_composite_expr_into_slot(plan,
                                                         lowering,
                                                         current_function,
                                                         aliases,
                                                         alias_count,
                                                         locals,
                                                         local_count,
                                                         callee,
                                                         i,
                                                         args[i],
                                                         &locals[local_index],
                                                         call_arg_base,
                                                         string_temp_base,
                                                         string_temp_stride,
                                                         string_label_index_io,
                                                         call_depth + 1,
                                                         out,
                                                         cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] composite call-into-address arg materialize failed caller=%s callee=%s arg_index=%zu expr=%s\n",
                        current_function->symbol_text,
                        callee,
                        i,
                        args[i]);
                return false;
            }
            if (!v3_emit_slot_address(out, cap, &locals[local_index], 15)) {
                fprintf(stderr,
                        "[cheng_v3_seed] composite call-into-address arg address failed caller=%s callee=%s arg_index=%zu local=%s\n",
                        current_function->symbol_text,
                        callee,
                        i,
                        locals[local_index].name);
                return false;
            }
            v3_emit_call_arg_spill_store(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         i,
                                         "ptr",
                                         15);
        }
    }
    v3_emit_call_dest_spill_load(out,
                                 cap,
                                 call_arg_base,
                                 call_depth,
                                 8);
    if (arg_count > CHENG_V3_CALL_ARG_REGS) {
        int32_t stack_arg_bytes =
            v3_align_up_i32((int32_t)(arg_count - CHENG_V3_CALL_ARG_REGS) * 8, 16);
        v3_text_appendf(out, cap, "  sub sp, sp, #%d\n", stack_arg_bytes);
        for (i = CHENG_V3_CALL_ARG_REGS; i < arg_count; ++i) {
            const char *stack_abi =
                v3_abi_class_scalar_or_ptr(target.param_abi_classes[i]) ?
                target.param_abi_classes[i] : "ptr";
            int32_t stack_offset = (int32_t)(i - CHENG_V3_CALL_ARG_REGS) * 8;
            v3_emit_call_arg_spill_load(out,
                                        cap,
                                        call_arg_base,
                                        call_depth,
                                        i,
                                        9,
                                        stack_abi);
            if (strcmp(stack_abi, "i32") == 0 || strcmp(stack_abi, "bool") == 0) {
                v3_text_appendf(out, cap, "  str w9, [sp, #%d]\n", stack_offset);
            } else {
                v3_text_appendf(out, cap, "  str x9, [sp, #%d]\n", stack_offset);
            }
        }
    }
    for (i = 0; i < arg_count && i < CHENG_V3_CALL_ARG_REGS; ++i) {
        v3_emit_call_arg_spill_load(out,
                                    cap,
                                    call_arg_base,
                                    call_depth,
                                    i,
                                    (int)i,
                                    v3_abi_class_scalar_or_ptr(target.param_abi_classes[i]) ?
                                    target.param_abi_classes[i] : "ptr");
    }
    v3_text_appendf(out, cap, "  bl %s\n", target.symbol_name);
    if (arg_count > CHENG_V3_CALL_ARG_REGS) {
        int32_t stack_arg_bytes =
            v3_align_up_i32((int32_t)(arg_count - CHENG_V3_CALL_ARG_REGS) * 8, 16);
        v3_text_appendf(out, cap, "  add sp, sp, #%d\n", stack_arg_bytes);
    }
    return true;
}

static bool v3_emit_constructor_into_address(const V3SystemLinkPlanStub *plan,
                                             const V3LoweringPlanStub *lowering,
                                             const V3LoweredFunctionStub *current_function,
                                             const V3ImportAlias *aliases,
                                             size_t alias_count,
                                             const V3AsmLocalSlot *locals,
                                             size_t local_count,
                                             const char *callee,
                                             char args[][4096],
                                             size_t arg_count,
                                             const char *dest_type_text,
                                             int dest_addr_reg,
                                             int32_t call_arg_base,
                                             int32_t string_temp_base,
                                             int32_t string_temp_stride,
                                             int32_t *string_label_index_io,
                                             int call_depth,
                                             char *out,
                                             size_t cap) {
    V3TypeLayoutStub layout;
    size_t i;
    (void)callee;
    if (!v3_compute_type_layout_impl(lowering, dest_type_text, &layout, 0U) ||
        !v3_emit_zero_address_range(out, cap, dest_addr_reg, layout.size)) {
        return false;
    }
    v3_emit_call_dest_spill_store(out,
                                  cap,
                                  call_arg_base,
                                  call_depth,
                                  dest_addr_reg);
    for (i = 0; i < arg_count; ++i) {
        char field_name[128];
        char field_expr[4096];
        char field_type[256];
        char field_abi[32];
        int32_t field_offset = 0;
        int field_addr_reg = dest_addr_reg == 14 ? 15 : 14;
        if (!v3_parse_named_arg_meta(args[i],
                                     field_name,
                                     sizeof(field_name),
                                     field_expr,
                                     sizeof(field_expr)) ||
            !v3_resolve_field_meta(lowering,
                                   dest_type_text,
                                   field_name,
                                   field_type,
                                   sizeof(field_type),
                                   field_abi,
                                   sizeof(field_abi),
                                   &field_offset)) {
            return false;
        }
        if (v3_abi_class_scalar_or_ptr(field_abi)) {
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        current_function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        field_expr,
                                        field_abi,
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        call_depth + 1,
                                        out,
                                        cap)) {
                return false;
            }
            v3_emit_call_dest_spill_load(out,
                                         cap,
                                         call_arg_base,
                                         call_depth,
                                         dest_addr_reg);
            v3_emit_store_scalar_to_address(out, cap, dest_addr_reg, field_offset, field_abi, 9);
            continue;
        }
        v3_emit_call_dest_spill_load(out,
                                     cap,
                                     call_arg_base,
                                     call_depth,
                                     field_addr_reg);
        if (field_offset != 0 &&
            !v3_emit_add_address_offset(out, cap, field_addr_reg, field_addr_reg, field_offset)) {
            return false;
        }
        if (!v3_materialize_composite_expr_into_address(plan,
                                                        lowering,
                                                        current_function,
                                                        aliases,
                                                        alias_count,
                                                        locals,
                                                        local_count,
                                                        field_expr,
                                                        field_type,
                                                        field_addr_reg,
                                                        call_arg_base,
                                                        string_temp_base,
                                                        string_temp_stride,
                                                        string_label_index_io,
                                                        call_depth + 1,
                                                        out,
                                                        cap)) {
            return false;
        }
    }
    return true;
}

static bool v3_materialize_composite_expr_into_address(const V3SystemLinkPlanStub *plan,
                                                       const V3LoweringPlanStub *lowering,
                                                       const V3LoweredFunctionStub *current_function,
                                                       const V3ImportAlias *aliases,
                                                       size_t alias_count,
                                                       const V3AsmLocalSlot *locals,
                                                       size_t local_count,
                                                       const char *expr_text,
                                                       const char *dest_type_text,
                                                       int dest_addr_reg,
                                                       int32_t call_arg_base,
                                                       int32_t string_temp_base,
                                                       int32_t string_temp_stride,
                                                       int32_t *string_label_index_io,
                                                       int call_depth,
                                                       char *out,
                                                       size_t cap) {
    char expr[4096];
    char normalized_dest_type[256];
    char literal[4096];
    char left_expr[4096];
    char right_expr[4096];
    char ternary_cond[4096];
    char ternary_true[4096];
    char ternary_false[4096];
    char inner_callee[PATH_MAX];
    char arg_text[4096];
    char args[CHENG_V3_MAX_CALL_ARGS][4096];
    size_t arg_count = 0U;
    int32_t local_index;
    V3TypeLayoutStub layout;
    v3_trim_copy_text(expr_text, expr, sizeof(expr));
    if (expr[0] == '\0') {
        return false;
    }
    if (!v3_normalize_type_text(lowering,
                                current_function->owner_module_path,
                                aliases,
                                alias_count,
                                dest_type_text,
                                normalized_dest_type,
                                sizeof(normalized_dest_type))) {
        return false;
    }
    if (v3_parse_ternary_expr(expr,
                              ternary_cond,
                              sizeof(ternary_cond),
                              ternary_true,
                              sizeof(ternary_true),
                              ternary_false,
                              sizeof(ternary_false))) {
        char true_type[256];
        char true_abi[32];
        char false_type[256];
        char false_abi[32];
        char normalized_true_type[256];
        char normalized_false_type[256];
        int32_t label_id = v3_next_expr_label_id();
        char false_label[128];
        char end_label[128];
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                ternary_true,
                                true_type,
                                sizeof(true_type),
                                true_abi,
                                sizeof(true_abi)) ||
            !v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                ternary_false,
                                false_type,
                                sizeof(false_type),
                                false_abi,
                                sizeof(false_abi)) ||
            strcmp(true_abi, "composite") != 0 ||
            strcmp(false_abi, "composite") != 0 ||
            !v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    true_type,
                                    normalized_true_type,
                                    sizeof(normalized_true_type)) ||
            !v3_normalize_type_text(lowering,
                                    current_function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    false_type,
                                    normalized_false_type,
                                    sizeof(normalized_false_type)) ||
            strcmp(normalized_true_type, normalized_dest_type) != 0 ||
            strcmp(normalized_false_type, normalized_dest_type) != 0 ||
            !v3_codegen_expr_scalar(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    ternary_cond,
                                    "bool",
                                    9,
                                    call_arg_base,
                                    string_temp_base,
                                    string_temp_stride,
                                    call_depth + 1,
                                    out,
                                    cap)) {
            return false;
        }
        snprintf(false_label, sizeof(false_label), "L_v3_cexpr_false_%d", label_id);
        snprintf(end_label, sizeof(end_label), "L_v3_cexpr_end_%d", label_id);
        v3_text_appendf(out, cap,
                        "  cmp w9, #0\n"
                        "  b.eq %s\n",
                        false_label);
        if (!v3_materialize_composite_expr_into_address(plan,
                                                        lowering,
                                                        current_function,
                                                        aliases,
                                                        alias_count,
                                                        locals,
                                                        local_count,
                                                        ternary_true,
                                                        normalized_dest_type,
                                                        dest_addr_reg,
                                                        call_arg_base,
                                                        string_temp_base,
                                                        string_temp_stride,
                                                        string_label_index_io,
                                                        call_depth + 1,
                                                        out,
                                                        cap)) {
            return false;
        }
        v3_text_appendf(out, cap,
                        "  b %s\n"
                        "%s:\n",
                        end_label,
                        false_label);
        if (!v3_materialize_composite_expr_into_address(plan,
                                                        lowering,
                                                        current_function,
                                                        aliases,
                                                        alias_count,
                                                        locals,
                                                        local_count,
                                                        ternary_false,
                                                        normalized_dest_type,
                                                        dest_addr_reg,
                                                        call_arg_base,
                                                        string_temp_base,
                                                        string_temp_stride,
                                                        string_label_index_io,
                                                        call_depth + 1,
                                                        out,
                                                        cap)) {
            return false;
        }
        v3_text_appendf(out, cap, "%s:\n", end_label);
        return true;
    }
    if (v3_parse_string_literal_text(expr, literal, sizeof(literal))) {
        return strcmp(normalized_dest_type, "str") == 0 &&
               string_label_index_io &&
               v3_emit_str_literal_into_address(current_function,
                                               literal,
                                               dest_addr_reg,
                                               string_label_index_io,
                                               out,
                                               cap);
    }
    if (strcmp(normalized_dest_type, "str") == 0) {
        int32_t plus_index = v3_find_top_level_binary_op_last(expr, "+");
        if (plus_index >= 0) {
            V3AsmLocalSlot scratch_slot;
            char field_type[256];
            char field_abi[32];
            int32_t data_offset = 0;
            int32_t len_offset = 0;
            int32_t store_id_offset = 0;
            int32_t flags_offset = 0;
            int scratch_addr_reg = dest_addr_reg == 14 ? 15 : 14;
            snprintf(left_expr, sizeof(left_expr), "%.*s", plus_index, expr);
            snprintf(right_expr, sizeof(right_expr), "%s", expr + plus_index + 1);
            v3_trim_copy_text(left_expr, left_expr, sizeof(left_expr));
            v3_trim_copy_text(right_expr, right_expr, sizeof(right_expr));
            if (string_temp_stride < 24) {
                return false;
            }
            memset(&scratch_slot, 0, sizeof(scratch_slot));
            snprintf(scratch_slot.name, sizeof(scratch_slot.name), "%s", "__v3_str_concat_rhs");
            snprintf(scratch_slot.type_text, sizeof(scratch_slot.type_text), "%s", "str");
            snprintf(scratch_slot.abi_class, sizeof(scratch_slot.abi_class), "%s", "composite");
            scratch_slot.stack_offset = string_temp_base + call_depth * string_temp_stride;
            scratch_slot.stack_size = 24;
            scratch_slot.stack_align = 8;
            if (!v3_materialize_composite_expr_into_address(plan,
                                                            lowering,
                                                            current_function,
                                                            aliases,
                                                            alias_count,
                                                            locals,
                                                            local_count,
                                                            left_expr,
                                                            "str",
                                                            dest_addr_reg,
                                                            call_arg_base,
                                                            string_temp_base,
                                                            string_temp_stride,
                                                            string_label_index_io,
                                                            call_depth + 1,
                                                            out,
                                                            cap) ||
                !v3_emit_slot_address(out, cap, &scratch_slot, scratch_addr_reg) ||
                !v3_materialize_composite_expr_into_address(plan,
                                                            lowering,
                                                            current_function,
                                                            aliases,
                                                            alias_count,
                                                            locals,
                                                            local_count,
                                                            right_expr,
                                                            "str",
                                                            scratch_addr_reg,
                                                            call_arg_base,
                                                            string_temp_base,
                                                            string_temp_stride,
                                                            string_label_index_io,
                                                            call_depth + 1,
                                                            out,
                                                            cap) ||
                !v3_resolve_field_meta(lowering,
                                       "str",
                                       "data",
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       &data_offset) ||
                strcmp(field_abi, "ptr") != 0 ||
                !v3_resolve_field_meta(lowering,
                                       "str",
                                       "len",
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       &len_offset) ||
                strcmp(field_abi, "i32") != 0 ||
                !v3_resolve_field_meta(lowering,
                                       "str",
                                       "store_id",
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       &store_id_offset) ||
                strcmp(field_abi, "i32") != 0 ||
                !v3_resolve_field_meta(lowering,
                                       "str",
                                       "flags",
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       &flags_offset) ||
                strcmp(field_abi, "i32") != 0) {
                return false;
            }
            v3_text_appendf(out, cap,
                            "  mov x0, x%d\n"
                            "  bl _cheng_str_param_to_cstring_compat\n"
                            "  mov x10, x0\n"
                            "  mov x0, x%d\n"
                            "  bl _cheng_str_param_to_cstring_compat\n"
                            "  mov x1, x0\n"
                            "  mov x0, x10\n"
                            "  bl ___cheng_str_concat\n"
                            "  mov x10, x0\n"
                            "  mov x0, x10\n"
                            "  bl _cheng_cstrlen\n"
                            "  mov x11, x0\n",
                            dest_addr_reg,
                            scratch_addr_reg);
            v3_emit_store_scalar_to_address(out, cap, dest_addr_reg, data_offset, "ptr", 10);
            v3_emit_store_scalar_to_address(out, cap, dest_addr_reg, len_offset, "i32", 11);
            if (!v3_emit_mov_imm(out, cap, 12, 0) ||
                !v3_emit_mov_imm(out, cap, 13, 1)) {
                return false;
            }
            v3_emit_store_scalar_to_address(out, cap, dest_addr_reg, store_id_offset, "i32", 12);
            v3_emit_store_scalar_to_address(out, cap, dest_addr_reg, flags_offset, "i32", 13);
            return true;
        }
    }
    if (strcmp(expr, "[]") == 0) {
        return strlen(normalized_dest_type) > 2U &&
               strcmp(normalized_dest_type + strlen(normalized_dest_type) - 2U, "[]") == 0 &&
               v3_compute_type_layout_impl(lowering, normalized_dest_type, &layout, 0U) &&
               v3_emit_zero_address_range(out, cap, dest_addr_reg, layout.size);
    }
    if (v3_parse_list_literal_text(expr, args, &arg_count, CHENG_V3_MAX_CALL_ARGS) &&
        arg_count > 0U &&
        strlen(normalized_dest_type) > 2U &&
        strcmp(normalized_dest_type + strlen(normalized_dest_type) - 2U, "[]") == 0) {
        char elem_type[256];
        char elem_abi[32];
        int32_t elem_size = 0;
        int buffer_reg = dest_addr_reg == 14 ? 13 : 14;
        size_t i;
        snprintf(elem_type,
                 sizeof(elem_type),
                 "%.*s",
                 (int)(strlen(normalized_dest_type) - 2U),
                 normalized_dest_type);
        snprintf(elem_abi, sizeof(elem_abi), "%s", v3_type_abi_class(elem_type));
        if (strcmp(elem_abi, "i32") == 0 || strcmp(elem_abi, "bool") == 0) {
            elem_size = 4;
        } else if (strcmp(elem_abi, "i64") == 0 || strcmp(elem_abi, "ptr") == 0) {
            elem_size = 8;
        } else if (!v3_compute_type_layout_impl(lowering, elem_type, &layout, 0U)) {
            return false;
        } else {
            elem_size = layout.size;
        }
        if (elem_size <= 0 ||
            !v3_emit_mov_imm(out, cap, 0, (int64_t)elem_size * (int64_t)arg_count)) {
            return false;
        }
        v3_text_appendf(out, cap, "  bl _alloc\n");
        if (!v3_emit_mov_imm(out, cap, 9, (int64_t)arg_count)) {
            return false;
        }
        v3_text_appendf(out, cap,
                        "  str w9, [x%d, #0]\n"
                        "  str w9, [x%d, #4]\n"
                        "  str x0, [x%d, #8]\n",
                        dest_addr_reg,
                        dest_addr_reg,
                        dest_addr_reg);
        for (i = 0; i < arg_count; ++i) {
            int32_t elem_offset = elem_size * (int32_t)i;
            if (strcmp(elem_abi, "i32") == 0 || strcmp(elem_abi, "bool") == 0 ||
                strcmp(elem_abi, "i64") == 0 || strcmp(elem_abi, "ptr") == 0) {
                if (!v3_codegen_expr_scalar(plan,
                                            lowering,
                                            current_function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            args[i],
                                            elem_abi,
                                            9,
                                            call_arg_base,
                                            string_temp_base,
                                            string_temp_stride,
                                            call_depth + 1,
                                            out,
                                            cap)) {
                    return false;
                }
                v3_text_appendf(out, cap, "  ldr x%d, [x%d, #8]\n", buffer_reg, dest_addr_reg);
                if (strcmp(elem_abi, "bool") == 0) {
                    v3_text_appendf(out, cap, "  strb w9, [x%d, #%d]\n", buffer_reg, elem_offset);
                } else if (strcmp(elem_abi, "i32") == 0) {
                    v3_text_appendf(out, cap, "  str w9, [x%d, #%d]\n", buffer_reg, elem_offset);
                } else {
                    v3_text_appendf(out, cap, "  str x9, [x%d, #%d]\n", buffer_reg, elem_offset);
                }
                continue;
            }
            v3_text_appendf(out, cap, "  ldr x%d, [x%d, #8]\n", buffer_reg, dest_addr_reg);
            if (elem_offset == 0) {
                v3_text_appendf(out, cap, "  mov x15, x%d\n", buffer_reg);
            } else if (!v3_emit_add_address_offset(out, cap, 15, buffer_reg, elem_offset)) {
                return false;
            }
            if (!v3_materialize_composite_expr_into_address(plan,
                                                            lowering,
                                                            current_function,
                                                            aliases,
                                                            alias_count,
                                                            locals,
                                                            local_count,
                                                            args[i],
                                                            elem_type,
                                                            15,
                                                            call_arg_base,
                                                            string_temp_base,
                                                            string_temp_stride,
                                                            string_label_index_io,
                                                            call_depth + 1,
                                                            out,
                                                            cap)) {
                return false;
            }
        }
        return true;
    }
    if (v3_parse_call_text(expr, inner_callee, sizeof(inner_callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS) &&
        arg_count == 1U) {
        V3ResultIntrinsicKind result_kind =
            v3_resolve_result_intrinsic_kind(lowering,
                                             current_function->owner_module_path,
                                             aliases,
                                             alias_count,
                                             inner_callee);
        char field_type[256];
        char field_abi[32];
        int src_addr_reg = dest_addr_reg == 14 ? 15 : 14;
        if (result_kind == V3_RESULT_INTRINSIC_OK ||
            result_kind == V3_RESULT_INTRINSIC_ERR ||
            result_kind == V3_RESULT_INTRINSIC_ERR_INFO) {
            return v3_emit_result_constructor_into_address(plan,
                                                           lowering,
                                                           current_function,
                                                           aliases,
                                                           alias_count,
                                                           locals,
                                                           local_count,
                                                           result_kind,
                                                           inner_callee,
                                                           args,
                                                           arg_count,
                                                           normalized_dest_type,
                                                           dest_addr_reg,
                                                           call_arg_base,
                                                           string_temp_base,
                                                           string_temp_stride,
                                                           string_label_index_io,
                                                           call_depth,
                                                           out,
                                                           cap);
        }
        if (result_kind == V3_RESULT_INTRINSIC_VALUE) {
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              args[0],
                                              "value",
                                              src_addr_reg,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi)) ||
                strcmp(field_abi, "composite") != 0 ||
                !v3_compute_type_layout_impl(lowering, field_type, &layout, 0U)) {
                return false;
            }
            return v3_emit_copy_address_to_address(out, cap, dest_addr_reg, src_addr_reg, layout.size);
        }
        if (result_kind == V3_RESULT_INTRINSIC_ERROR_INFO_OF) {
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              args[0],
                                              "err",
                                              src_addr_reg,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi)) ||
                strcmp(field_abi, "composite") != 0 ||
                !v3_compute_type_layout_impl(lowering, field_type, &layout, 0U)) {
                return false;
            }
            return v3_emit_copy_address_to_address(out, cap, dest_addr_reg, src_addr_reg, layout.size);
        }
        if (result_kind == V3_RESULT_INTRINSIC_ERROR ||
            result_kind == V3_RESULT_INTRINSIC_ERROR_TEXT) {
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              args[0],
                                              "err",
                                              src_addr_reg,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi)) ||
                !v3_resolve_field_meta(lowering,
                                       field_type,
                                       "msg",
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       &local_index) ||
                strcmp(field_abi, "composite") != 0 ||
                !v3_compute_type_layout_impl(lowering, field_type, &layout, 0U)) {
                return false;
            }
            if (local_index != 0 &&
                !v3_emit_add_address_offset(out, cap, src_addr_reg, src_addr_reg, local_index)) {
                return false;
            }
            return v3_emit_copy_address_to_address(out, cap, dest_addr_reg, src_addr_reg, layout.size);
        }
    }
    local_index = v3_find_local_slot(locals, local_count, expr);
    if (local_index >= 0 && !v3_abi_class_scalar_or_ptr(locals[local_index].abi_class)) {
        return v3_emit_copy_slot_to_address(lowering, out, cap, &locals[local_index], dest_addr_reg);
    }
    {
        char field_type[256];
        char field_abi[32];
        int src_addr_reg = dest_addr_reg == 14 ? 15 : 14;
        if (v3_emit_index_access_address(plan,
                                         lowering,
                                         current_function,
                                         aliases,
                                         alias_count,
                                         locals,
                                         local_count,
                                         expr,
                                         field_type,
                                         sizeof(field_type),
                                         field_abi,
                                         sizeof(field_abi),
                                         src_addr_reg,
                                         call_arg_base,
                                         string_temp_base,
                                         string_temp_stride,
                                         call_depth,
                                         out,
                                         cap)) {
            if (v3_abi_class_scalar_or_ptr(field_abi) ||
                !v3_compute_type_layout_impl(lowering, field_type, &layout, 0U)) {
                return false;
            }
            return v3_emit_copy_address_to_address(out, cap, dest_addr_reg, src_addr_reg, layout.size);
        }
    }
    {
        char field_type[256];
        char field_abi[32];
        int src_addr_reg = dest_addr_reg == 14 ? 15 : 14;
        if (v3_emit_member_access_address(plan,
                                          lowering,
                                          current_function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          expr,
                                          field_type,
                                          sizeof(field_type),
                                          field_abi,
                                          sizeof(field_abi),
                                          src_addr_reg,
                                          call_arg_base,
                                          string_temp_base,
                                          string_temp_stride,
                                          call_depth,
                                          out,
                                          cap) ||
            v3_emit_field_path_address(out,
                                       cap,
                                       locals,
                                       local_count,
                                       lowering,
                                       expr,
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       src_addr_reg)) {
            if (v3_abi_class_scalar_or_ptr(field_abi) ||
                !v3_compute_type_layout_impl(lowering, field_type, &layout, 0U)) {
                return false;
            }
            return v3_emit_copy_address_to_address(out, cap, dest_addr_reg, src_addr_reg, layout.size);
        }
    }
    if (v3_parse_call_text(expr, inner_callee, sizeof(inner_callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
        V3AsmCallTarget target;
        const char *ctor_base = strrchr(inner_callee, '.');
        V3ResultIntrinsicKind result_kind =
            v3_resolve_result_intrinsic_kind(lowering,
                                             current_function->owner_module_path,
                                             aliases,
                                             alias_count,
                                             inner_callee);
        ctor_base = ctor_base ? ctor_base + 1 : inner_callee;
        if (result_kind == V3_RESULT_INTRINSIC_ERR_CODE) {
            return v3_emit_result_constructor_into_address(plan,
                                                           lowering,
                                                           current_function,
                                                           aliases,
                                                           alias_count,
                                                           locals,
                                                           local_count,
                                                           result_kind,
                                                           inner_callee,
                                                           args,
                                                           arg_count,
                                                           normalized_dest_type,
                                                           dest_addr_reg,
                                                           call_arg_base,
                                                           string_temp_base,
                                                           string_temp_stride,
                                                           string_label_index_io,
                                                           call_depth,
                                                           out,
                                                           cap);
        }
        if (v3_resolve_call_target(plan, lowering, current_function, aliases, alias_count, inner_callee, &target)) {
            if (strcmp(target.return_abi_class, "composite") != 0) {
                return false;
            }
            if (!v3_codegen_call_into_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              inner_callee,
                                              args,
                                              arg_count,
                                              dest_addr_reg,
                                              call_arg_base,
                                              string_temp_base,
                                              string_temp_stride,
                                              string_label_index_io,
                                              call_depth,
                                              out,
                                              cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] materialize composite call failed caller=%s callee=%s expr=%s dest_type=%s\n",
                        current_function->symbol_text,
                        inner_callee,
                        expr,
                        normalized_dest_type);
                return false;
            }
            return true;
        }
        if (ctor_base[0] >= 'A' && ctor_base[0] <= 'Z') {
            return v3_emit_constructor_into_address(plan,
                                                    lowering,
                                                    current_function,
                                                    aliases,
                                                    alias_count,
                                                    locals,
                                                    local_count,
                                                    inner_callee,
                                                    args,
                                                    arg_count,
                                                    normalized_dest_type,
                                                    dest_addr_reg,
                                                    call_arg_base,
                                                    string_temp_base,
                                                    string_temp_stride,
                                                    string_label_index_io,
                                                    call_depth,
                                                    out,
                                                    cap);
        }
        return false;
    }
    if (v3_parse_prefix_single_arg_call(expr, inner_callee, sizeof(inner_callee), arg_text, sizeof(arg_text))) {
        V3AsmCallTarget target;
        V3ResultIntrinsicKind result_kind =
            v3_resolve_result_intrinsic_kind(lowering,
                                             current_function->owner_module_path,
                                             aliases,
                                             alias_count,
                                             inner_callee);
        snprintf(args[0], sizeof(args[0]), "%s", arg_text);
        if (result_kind == V3_RESULT_INTRINSIC_OK ||
            result_kind == V3_RESULT_INTRINSIC_ERR ||
            result_kind == V3_RESULT_INTRINSIC_ERR_INFO) {
            return v3_emit_result_constructor_into_address(plan,
                                                           lowering,
                                                           current_function,
                                                           aliases,
                                                           alias_count,
                                                           locals,
                                                           local_count,
                                                           result_kind,
                                                           inner_callee,
                                                           args,
                                                           1U,
                                                           normalized_dest_type,
                                                           dest_addr_reg,
                                                           call_arg_base,
                                                           string_temp_base,
                                                           string_temp_stride,
                                                           string_label_index_io,
                                                           call_depth,
                                                           out,
                                                           cap);
        }
        if (!v3_resolve_call_target(plan, lowering, current_function, aliases, alias_count, inner_callee, &target) ||
            strcmp(target.return_abi_class, "composite") != 0) {
            return false;
        }
        return v3_codegen_call_into_address(plan,
                                            lowering,
                                            current_function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            inner_callee,
                                            args,
                                            1U,
                                            dest_addr_reg,
                                            call_arg_base,
                                            string_temp_base,
                                            string_temp_stride,
                                            string_label_index_io,
                                            call_depth,
                                            out,
                                            cap);
    }
    return false;
}

static bool v3_materialize_composite_expr_into_slot(const V3SystemLinkPlanStub *plan,
                                                    const V3LoweringPlanStub *lowering,
                                                    const V3LoweredFunctionStub *current_function,
                                                    const V3ImportAlias *aliases,
                                                    size_t alias_count,
                                                    const V3AsmLocalSlot *locals,
                                                    size_t local_count,
                                                    const char *callee,
                                                    size_t arg_index,
                                                    const char *expr_text,
                                                    const V3AsmLocalSlot *dest_slot,
                                                    int32_t call_arg_base,
                                                    int32_t string_temp_base,
                                                    int32_t string_temp_stride,
                                                    int32_t *string_label_index_io,
                                                    int call_depth,
                                                    char *out,
                                                    size_t cap) {
    (void)callee;
    (void)arg_index;
    if (!dest_slot || !v3_emit_slot_address(out, cap, dest_slot, 15)) {
        return false;
    }
    return v3_materialize_composite_expr_into_address(plan,
                                                      lowering,
                                                      current_function,
                                                      aliases,
                                                      alias_count,
                                                      locals,
                                                      local_count,
                                                      expr_text,
                                                      dest_slot->type_text,
                                                      15,
                                                      call_arg_base,
                                                      string_temp_base,
                                                      string_temp_stride,
                                                      string_label_index_io,
                                                      call_depth,
                                                      out,
                                                      cap);
}

static bool v3_try_emit_builtin_statement(const V3SystemLinkPlanStub *plan,
                                          const V3LoweringPlanStub *lowering,
                                          const V3LoweredFunctionStub *current_function,
                                          const V3ImportAlias *aliases,
                                          size_t alias_count,
                                          const V3AsmLocalSlot *locals,
                                          size_t local_count,
                                          const char *statement,
                                          int32_t call_arg_base,
                                          int32_t string_temp_base,
                                          int32_t string_temp_stride,
                                          int32_t *string_label_index_io,
                                          int32_t *control_label_index_io,
                                          char *out,
                                          size_t cap,
                                          bool *handled_out) {
    char callee[PATH_MAX];
    char args[CHENG_V3_MAX_CALL_ARGS][4096];
    size_t arg_count = 0U;
    (void)control_label_index_io;
    *handled_out = false;
    if (!v3_parse_call_text(statement, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
        return true;
    }
    if (strcmp(callee, "add") == 0 && arg_count == 2U) {
        char seq_type[256];
        char seq_abi[32];
        char elem_type[256];
        char elem_abi[32];
        char field_type[256];
        char field_abi[32];
        int32_t seq_local_index = -1;
        int32_t elem_size = 0;
        V3TypeLayoutStub elem_layout;
        *handled_out = true;
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                args[0],
                                seq_type,
                                sizeof(seq_type),
                                seq_abi,
                                sizeof(seq_abi)) ||
            strcmp(seq_abi, "composite") != 0 ||
            strlen(seq_type) <= 2U ||
            strcmp(seq_type + strlen(seq_type) - 2U, "[]") != 0) {
            return false;
        }
        snprintf(elem_type, sizeof(elem_type), "%.*s", (int)(strlen(seq_type) - 2U), seq_type);
        snprintf(elem_abi, sizeof(elem_abi), "%s", v3_type_abi_class(elem_type));
        if (strcmp(elem_abi, "bool") == 0) {
            elem_size = 1;
        } else if (strcmp(elem_abi, "i32") == 0) {
            elem_size = 4;
        } else if (strcmp(elem_abi, "i64") == 0 || strcmp(elem_abi, "ptr") == 0) {
            elem_size = 8;
        } else if (!v3_compute_type_layout_impl(lowering, elem_type, &elem_layout, 0U)) {
            return false;
        } else {
            elem_size = elem_layout.size;
        }
        if (elem_size <= 0) {
            return false;
        }
        seq_local_index = v3_find_local_slot(locals, local_count, args[0]);
        if (seq_local_index >= 0) {
            if (!v3_emit_slot_address(out, cap, &locals[seq_local_index], 15)) {
                return false;
            }
        } else if ((!v3_emit_member_access_address(plan,
                                                   lowering,
                                                   current_function,
                                                   aliases,
                                                   alias_count,
                                                   locals,
                                                   local_count,
                                                   args[0],
                                                   field_type,
                                                   sizeof(field_type),
                                                   field_abi,
                                                   sizeof(field_abi),
                                                   15,
                                                   call_arg_base,
                                                   string_temp_base,
                                                   string_temp_stride,
                                                   0,
                                                   out,
                                                   cap) &&
                    !v3_emit_field_path_address(out,
                                                cap,
                                                locals,
                                                local_count,
                                                lowering,
                                                args[0],
                                                field_type,
                                                sizeof(field_type),
                                                field_abi,
                                                sizeof(field_abi),
                                                15)) ||
                   strcmp(field_abi, "composite") != 0) {
            return false;
        }
        v3_text_appendf(out, cap,
                        "  ldrsw x10, [x15, #0]\n"
                        "  mov x0, x15\n"
                        "  mov x1, x10\n");
        if (!v3_emit_mov_imm(out, cap, 2, elem_size)) {
            return false;
        }
        v3_text_appendf(out, cap, "  bl _cheng_seq_set_grow\n");
        if (v3_abi_class_scalar_or_ptr(elem_abi)) {
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        current_function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        args[1],
                                        elem_abi,
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        0,
                                        out,
                                        cap)) {
                return false;
            }
            if (strcmp(elem_abi, "bool") == 0) {
                v3_text_appendf(out, cap, "  strb w9, [x0, #0]\n");
            } else if (strcmp(elem_abi, "i32") == 0) {
                v3_text_appendf(out, cap, "  str w9, [x0, #0]\n");
            } else {
                v3_text_appendf(out, cap, "  str x9, [x0, #0]\n");
            }
            return true;
        }
        v3_text_appendf(out, cap, "  mov x15, x0\n");
        return v3_materialize_composite_expr_into_address(plan,
                                                          lowering,
                                                          current_function,
                                                          aliases,
                                                          alias_count,
                                                          locals,
                                                          local_count,
                                                          args[1],
                                                          elem_type,
                                                          15,
                                                          call_arg_base,
                                                          string_temp_base,
                                                          string_temp_stride,
                                                          string_label_index_io,
                                                          0,
                                                          out,
                                                          cap);
    }
    if ((strcmp(callee, "panic") == 0 || strcmp(callee, "system.panic") == 0) && arg_count == 1U) {
        char literal[4096];
        char label[128];
        char arg_type[256];
        char arg_abi[32];
        char inner_callee[PATH_MAX];
        char inner_arg[4096];
        char inner_args[CHENG_V3_MAX_CALL_ARGS][4096];
        size_t inner_arg_count = 0U;
        char field_type[256];
        char field_abi[32];
        int32_t field_offset = 0;
        int32_t local_index = -1;
        *handled_out = true;
        if (v3_parse_string_literal_text(args[0], literal, sizeof(literal))) {
            if (!v3_emit_cstring_literal_label(current_function,
                                               literal,
                                               string_label_index_io,
                                               label,
                                               sizeof(label),
                                               out,
                                               cap)) {
                return false;
            }
            v3_text_appendf(out, cap,
                            "  adrp x0, %s@PAGE\n"
                            "  add x0, x0, %s@PAGEOFF\n"
                            "  bl _cheng_v3_panic_cstring_and_exit\n",
                            label,
                            label);
            return true;
        }
        if (((v3_parse_call_text(args[0],
                                 inner_callee,
                                 sizeof(inner_callee),
                                 inner_args,
                                 &inner_arg_count,
                                 CHENG_V3_MAX_CALL_ARGS) &&
              inner_arg_count == 1U &&
              (snprintf(inner_arg, sizeof(inner_arg), "%s", inner_args[0]), true)) ||
             v3_parse_prefix_single_arg_call(args[0], inner_callee, sizeof(inner_callee), inner_arg, sizeof(inner_arg))) &&
            (strcmp(inner_callee, "Error") == 0 || strcmp(inner_callee, "ErrorText") == 0)) {
            if (!v3_emit_result_field_address(plan,
                                              lowering,
                                              current_function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              inner_arg,
                                              "err",
                                              15,
                                              out,
                                              cap,
                                              field_type,
                                              sizeof(field_type),
                                              field_abi,
                                              sizeof(field_abi)) ||
                !v3_resolve_field_meta(lowering,
                                       field_type,
                                       "msg",
                                       field_type,
                                       sizeof(field_type),
                                       field_abi,
                                       sizeof(field_abi),
                                       &field_offset) ||
                strcmp(field_type, "str") != 0 ||
                strcmp(field_abi, "composite") != 0) {
                return false;
            }
            if (field_offset != 0) {
                if (!v3_emit_add_address_offset(out, cap, 15, 15, field_offset)) {
                    return false;
                }
            }
            v3_text_appendf(out, cap,
                            "  mov x0, x15\n"
                            "  bl _cheng_str_param_to_cstring_compat\n"
                            "  bl _cheng_v3_panic_cstring_and_exit\n");
            return true;
        }
        if (!v3_infer_expr_type(plan,
                                lowering,
                                current_function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                args[0],
                                arg_type,
                                sizeof(arg_type),
                                arg_abi,
                                sizeof(arg_abi))) {
            return false;
        }
        if (strcmp(arg_abi, "ptr") == 0) {
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        current_function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        args[0],
                                        "ptr",
                                        0,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        0,
                                        out,
                                        cap)) {
                return false;
            }
            v3_text_appendf(out, cap,
                            "  bl _cheng_v3_panic_cstring_and_exit\n");
            return true;
        }
        if (strcmp(arg_type, "str") != 0 || strcmp(arg_abi, "composite") != 0) {
            return false;
        }
        local_index = v3_find_local_slot(locals, local_count, args[0]);
        if (local_index >= 0) {
            if (!v3_emit_slot_address(out, cap, &locals[local_index], 15)) {
                return false;
            }
        } else if ((!v3_emit_member_access_address(plan,
                                                   lowering,
                                                   current_function,
                                                   aliases,
                                                   alias_count,
                                                   locals,
                                                   local_count,
                                                   args[0],
                                                   field_type,
                                                   sizeof(field_type),
                                                   field_abi,
                                                   sizeof(field_abi),
                                                   15,
                                                   call_arg_base,
                                                   string_temp_base,
                                                   string_temp_stride,
                                                   0,
                                                   out,
                                                   cap) &&
                    !v3_emit_field_path_address(out,
                                                cap,
                                                locals,
                                                local_count,
                                                lowering,
                                                args[0],
                                                field_type,
                                                sizeof(field_type),
                                                field_abi,
                                                sizeof(field_abi),
                                                15)) ||
                   strcmp(field_type, "str") != 0 ||
                   strcmp(field_abi, "composite") != 0) {
            return false;
        }
        v3_text_appendf(out, cap,
                        "  mov x0, x15\n"
                        "  bl _cheng_str_param_to_cstring_compat\n"
                        "  bl _cheng_v3_panic_cstring_and_exit\n");
        return true;
    }
    if (strcmp(callee, "echo") == 0) {
        char literal[4096];
        char label[128];
        *handled_out = true;
        if (arg_count != 1U ||
            !v3_parse_string_literal_text(args[0], literal, sizeof(literal)) ||
            !v3_emit_cstring_literal_label(current_function,
                                           literal,
                                           string_label_index_io,
                                           label,
                                           sizeof(label),
                                           out,
                                           cap)) {
            return false;
        }
        v3_text_appendf(out, cap,
                        "  adrp x0, %s@PAGE\n"
                        "  add x0, x0, %s@PAGEOFF\n"
                        "  bl _puts\n",
                        label,
                        label);
        return true;
    }
    if (strcmp(callee, "assert") == 0) {
        char ok_label[128];
        int32_t label_id = v3_next_expr_label_id();
        char literal[4096];
        char label[128];
        *handled_out = true;
        if ((arg_count != 1U && arg_count != 2U) ||
            !v3_codegen_expr_scalar(plan,
                                    lowering,
                                    current_function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    args[0],
                                    "bool",
                                    9,
                                    call_arg_base,
                                    string_temp_base,
                                    string_temp_stride,
                                    0,
                                    out,
                                    cap)) {
            return false;
        }
        snprintf(ok_label, sizeof(ok_label), "L_v3_assert_ok_%d", label_id);
        v3_text_appendf(out, cap,
                        "  cmp w9, #0\n"
                        "  b.ne %s\n",
                        ok_label);
        if (arg_count == 2U) {
            if (!v3_parse_string_literal_text(args[1], literal, sizeof(literal))) {
                return false;
            }
        } else {
            snprintf(literal, sizeof(literal), "%s", "assert failed");
        }
        if (!v3_emit_cstring_literal_label(current_function,
                                           literal,
                                           string_label_index_io,
                                           label,
                                           sizeof(label),
                                           out,
                                           cap)) {
            return false;
        }
        v3_text_appendf(out, cap,
                        "  adrp x0, %s@PAGE\n"
                        "  add x0, x0, %s@PAGEOFF\n"
                        "  bl _cheng_v3_panic_cstring_and_exit\n"
                        "%s:\n",
                        label,
                        label,
                        ok_label);
        return true;
    }
    return true;
}

static int32_t v3_find_top_level_colon(const char *text) {
    bool in_string = false;
    int32_t paren_depth = 0;
    int32_t bracket_depth = 0;
    size_t i;
    for (i = 0; text && text[i] != '\0'; ++i) {
        char ch = text[i];
        if (ch == '"' && (i == 0 || text[i - 1] != '\\')) {
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            continue;
        }
        if (ch == '(') {
            paren_depth += 1;
            continue;
        }
        if (ch == ')') {
            paren_depth -= 1;
            continue;
        }
        if (ch == '[') {
            bracket_depth += 1;
            continue;
        }
        if (ch == ']') {
            bracket_depth -= 1;
            continue;
        }
        if (paren_depth == 0 && bracket_depth == 0 && ch == ':') {
            return (int32_t)i;
        }
    }
    return -1;
}

static bool v3_parse_if_like_header(const char *statement,
                                    const char *keyword,
                                    char *cond_out,
                                    size_t cond_cap,
                                    char *inline_stmt_out,
                                    size_t inline_stmt_cap) {
    char trimmed[8192];
    int32_t colon;
    size_t keyword_len = strlen(keyword);
    if (!v3_startswith(statement, keyword)) {
        return false;
    }
    v3_trim_copy_text(statement, trimmed, sizeof(trimmed));
    colon = v3_find_top_level_colon(trimmed);
    if (colon < 0) {
        return false;
    }
    snprintf(cond_out, cond_cap, "%.*s", colon - (int32_t)keyword_len, trimmed + keyword_len);
    v3_trim_copy_text(cond_out, cond_out, cond_cap);
    snprintf(inline_stmt_out, inline_stmt_cap, "%s", trimmed + colon + 1);
    v3_trim_copy_text(inline_stmt_out, inline_stmt_out, inline_stmt_cap);
    return cond_out[0] != '\0';
}

static bool v3_parse_if_header(const char *statement,
                               char *cond_out,
                               size_t cond_cap,
                               char *inline_stmt_out,
                               size_t inline_stmt_cap) {
    return v3_parse_if_like_header(statement,
                                   "if ",
                                   cond_out,
                                   cond_cap,
                                   inline_stmt_out,
                                   inline_stmt_cap);
}

static bool v3_parse_elif_header(const char *statement,
                                 char *cond_out,
                                 size_t cond_cap,
                                 char *inline_stmt_out,
                                 size_t inline_stmt_cap) {
    return v3_parse_if_like_header(statement,
                                   "elif ",
                                   cond_out,
                                   cond_cap,
                                   inline_stmt_out,
                                   inline_stmt_cap);
}

static bool v3_parse_else_header(const char *statement,
                                 char *inline_stmt_out,
                                 size_t inline_stmt_cap) {
    char trimmed[8192];
    int32_t colon;
    if (!v3_startswith(statement, "else")) {
        return false;
    }
    v3_trim_copy_text(statement, trimmed, sizeof(trimmed));
    colon = v3_find_top_level_colon(trimmed);
    if (colon < 0) {
        return false;
    }
    snprintf(inline_stmt_out, inline_stmt_cap, "%s", trimmed + colon + 1);
    v3_trim_copy_text(inline_stmt_out, inline_stmt_out, inline_stmt_cap);
    return true;
}

static bool v3_parse_case_header(const char *statement,
                                 char *expr_out,
                                 size_t expr_cap) {
    if (!v3_startswith(statement, "case ")) {
        return false;
    }
    v3_trim_copy_text(statement + 5, expr_out, expr_cap);
    return expr_out[0] != '\0';
}

static bool v3_parse_case_arm_header(const char *statement,
                                     bool *is_else_out,
                                     int64_t *match_value_out,
                                     char *inline_stmt_out,
                                     size_t inline_stmt_cap) {
    char trimmed[8192];
    int32_t colon;
    char value_text[128];
    *is_else_out = false;
    *match_value_out = 0;
    inline_stmt_out[0] = '\0';
    v3_trim_copy_text(statement, trimmed, sizeof(trimmed));
    colon = v3_find_top_level_colon(trimmed);
    if (colon < 0) {
        return false;
    }
    if (v3_startswith(trimmed, "of ")) {
        snprintf(value_text, sizeof(value_text), "%.*s", colon - 3, trimmed + 3);
        v3_trim_copy_text(value_text, value_text, sizeof(value_text));
        if (value_text[0] == '\0') {
            return false;
        }
        *match_value_out = strtoll(value_text, NULL, 0);
    } else if (v3_startswith(trimmed, "else")) {
        *is_else_out = true;
    } else {
        return false;
    }
    snprintf(inline_stmt_out, inline_stmt_cap, "%s", trimmed + colon + 1);
    v3_trim_copy_text(inline_stmt_out, inline_stmt_out, inline_stmt_cap);
    return inline_stmt_out[0] != '\0';
}

static bool v3_parse_while_header(const char *statement,
                                  char *cond_out,
                                  size_t cond_cap,
                                  char *inline_stmt_out,
                                  size_t inline_stmt_cap) {
    char trimmed[8192];
    int32_t colon;
    if (!v3_startswith(statement, "while ")) {
        return false;
    }
    v3_trim_copy_text(statement, trimmed, sizeof(trimmed));
    colon = v3_find_top_level_colon(trimmed);
    if (colon < 0) {
        return false;
    }
    snprintf(cond_out, cond_cap, "%.*s", colon - 6, trimmed + 6);
    v3_trim_copy_text(cond_out, cond_out, cond_cap);
    snprintf(inline_stmt_out, inline_stmt_cap, "%s", trimmed + colon + 1);
    v3_trim_copy_text(inline_stmt_out, inline_stmt_out, inline_stmt_cap);
    return cond_out[0] != '\0';
}

static bool v3_parse_for_header(const char *statement,
                                char *iter_name_out,
                                size_t iter_name_cap,
                                char *start_expr_out,
                                size_t start_expr_cap,
                                char *end_expr_out,
                                size_t end_expr_cap,
                                char *inline_stmt_out,
                                size_t inline_stmt_cap) {
    char trimmed[8192];
    int32_t colon;
    char header[8192];
    char *in_sep;
    char *range_text;
    int32_t range_sep;
    if (!v3_startswith(statement, "for ")) {
        fprintf(stderr, "[cheng_v3_seed] parse for header reject prefix stmt=%s\n", statement);
        return false;
    }
    v3_trim_copy_text(statement, trimmed, sizeof(trimmed));
    colon = v3_find_top_level_colon(trimmed);
    if (colon < 0) {
        fprintf(stderr, "[cheng_v3_seed] parse for header missing colon stmt=%s trimmed=%s\n",
                statement,
                trimmed);
        return false;
    }
    snprintf(header, sizeof(header), "%.*s", colon - 4, trimmed + 4);
    v3_trim_copy_text(header, header, sizeof(header));
    in_sep = strstr(header, " in ");
    if (!in_sep) {
        fprintf(stderr, "[cheng_v3_seed] parse for header missing in-sep stmt=%s header=%s\n",
                statement,
                header);
        return false;
    }
    *in_sep = '\0';
    range_text = in_sep + 4;
    v3_trim_copy_text(header, iter_name_out, iter_name_cap);
    range_sep = v3_find_top_level_binary_op(range_text, "..<");
    if (range_sep < 0) {
        fprintf(stderr, "[cheng_v3_seed] parse for header missing range-sep stmt=%s range=%s\n",
                statement,
                range_text);
        return false;
    }
    snprintf(start_expr_out, start_expr_cap, "%.*s", range_sep, range_text);
    snprintf(end_expr_out, end_expr_cap, "%s", range_text + range_sep + 3);
    v3_trim_copy_text(start_expr_out, start_expr_out, start_expr_cap);
    v3_trim_copy_text(end_expr_out, end_expr_out, end_expr_cap);
    snprintf(inline_stmt_out, inline_stmt_cap, "%s", trimmed + colon + 1);
    v3_trim_copy_text(inline_stmt_out, inline_stmt_out, inline_stmt_cap);
    if (iter_name_out[0] == '\0' || start_expr_out[0] == '\0' || end_expr_out[0] == '\0') {
        fprintf(stderr,
                "[cheng_v3_seed] parse for header empty part stmt=%s iter=%s start=%s end=%s\n",
                statement,
                iter_name_out,
                start_expr_out,
                end_expr_out);
        return false;
    }
    return true;
}

static bool v3_prepare_if_statement_state(const V3SystemLinkPlanStub *plan,
                                          const V3LoweringPlanStub *lowering,
                                          const V3LoweredFunctionStub *function,
                                          const V3ImportAlias *aliases,
                                          size_t alias_count,
                                          V3AsmLocalSlot *locals,
                                          size_t *local_count,
                                          size_t local_cap,
                                          int32_t *next_offset_io,
                                          char **lines,
                                          size_t line_count,
                                          size_t *index_io,
                                          int32_t parent_indent,
                                          const char *statement,
                                          int32_t *max_call_depth_io,
                                          int32_t *max_string_literal_temps_io);
static bool v3_prepare_while_statement_state(const V3SystemLinkPlanStub *plan,
                                             const V3LoweringPlanStub *lowering,
                                             const V3LoweredFunctionStub *function,
                                             const V3ImportAlias *aliases,
                                             size_t alias_count,
                                             V3AsmLocalSlot *locals,
                                             size_t *local_count,
                                             size_t local_cap,
                                             int32_t *next_offset_io,
                                             char **lines,
                                             size_t line_count,
                                             size_t *index_io,
                                             int32_t parent_indent,
                                             const char *statement,
                                             int32_t *max_call_depth_io,
                                             int32_t *max_string_literal_temps_io);
static bool v3_prepare_for_statement_state(const V3SystemLinkPlanStub *plan,
                                           const V3LoweringPlanStub *lowering,
                                           const V3LoweredFunctionStub *function,
                                           const V3ImportAlias *aliases,
                                           size_t alias_count,
                                           V3AsmLocalSlot *locals,
                                           size_t *local_count,
                                           size_t local_cap,
                                           int32_t *next_offset_io,
                                           char **lines,
                                           size_t line_count,
                                           size_t *index_io,
                                           int32_t parent_indent,
                                           const char *statement,
                                           int32_t *max_call_depth_io,
                                           int32_t *max_string_literal_temps_io);
static bool v3_prepare_case_const_table_statement(const V3SystemLinkPlanStub *plan,
                                                  const V3LoweringPlanStub *lowering,
                                                  const V3LoweredFunctionStub *function,
                                                  const V3ImportAlias *aliases,
                                                  size_t alias_count,
                                                  V3AsmLocalSlot *locals,
                                                  size_t *local_count,
                                                  size_t local_cap,
                                                  int32_t *next_offset_io,
                                                  char **lines,
                                                  size_t line_count,
                                                  size_t *index_io,
                                                  int32_t parent_indent,
                                                  const char *statement,
                                                  int32_t *max_call_depth_io,
                                                  int32_t *max_string_literal_temps_io);

static bool v3_prepare_non_if_statement_state(const V3SystemLinkPlanStub *plan,
                                              const V3LoweringPlanStub *lowering,
                                              const V3LoweredFunctionStub *function,
                                              const V3ImportAlias *aliases,
                                              size_t alias_count,
                                              V3AsmLocalSlot *locals,
                                              size_t *local_count,
                                              size_t local_cap,
                                              int32_t *next_offset_io,
                                              const char *statement,
                                              int32_t *max_call_depth_io,
                                              int32_t *max_string_literal_temps_io) {
    char name[128];
    char type_text[128];
    char expr[4096];
    if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
        v3_parse_binding_decl_meta(statement,
                                   v3_startswith(statement, "let ") ? "let " : "var ",
                                   name,
                                   sizeof(name),
                                   type_text,
                                   sizeof(type_text))) {
        const char *abi_class = v3_type_abi_class(type_text);
        if (v3_find_local_slot(locals, *local_count, name) < 0) {
            return v3_add_local_slot_for_type(lowering,
                                              function->owner_module_path,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              local_cap,
                                              name,
                                              type_text,
                                              abi_class,
                                              false,
                                              next_offset_io);
        }
        return true;
    }
    if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
        v3_parse_binding_meta(statement,
                              v3_startswith(statement, "let ") ? "let " : "var ",
                              name,
                              sizeof(name),
                              type_text,
                              sizeof(type_text),
                              expr,
                              sizeof(expr))) {
        const char *abi_class;
        char inferred_type[128];
        char inferred_abi[32];
        if (type_text[0] == '\0') {
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    *local_count,
                                    expr,
                                    inferred_type,
                                    sizeof(inferred_type),
                                    inferred_abi,
                                    sizeof(inferred_abi))) {
                return false;
            }
            snprintf(type_text, sizeof(type_text), "%s", inferred_type);
        }
        abi_class = v3_type_abi_class(type_text);
        if (type_text[0] == '\0' || strcmp(abi_class, "void") == 0) {
            return false;
        }
        if (v3_find_local_slot(locals, *local_count, name) < 0 &&
            !v3_add_local_slot_for_type(lowering,
                                        function->owner_module_path,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        local_cap,
                                        name,
                                        type_text,
                                        abi_class,
                                        false,
                                        next_offset_io)) {
            return false;
        }
        if (v3_call_expr_string_literal_count(expr) > *max_string_literal_temps_io) {
            *max_string_literal_temps_io = v3_call_expr_string_literal_count(expr);
        }
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          expr,
                                          0,
                                          max_call_depth_io);
    }
    if (strcmp(statement, "continue") == 0 || strcmp(statement, "break") == 0) {
        return true;
    }
    if (strcmp(statement, "return") == 0) {
        return true;
    }
    if (v3_startswith(statement, "return ")) {
        if (v3_call_expr_string_literal_count(statement + 7) > *max_string_literal_temps_io) {
            *max_string_literal_temps_io = v3_call_expr_string_literal_count(statement + 7);
        }
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          statement + 7,
                                          0,
                                          max_call_depth_io);
    }
    if (v3_parse_assignment_meta(statement,
                                 name,
                                 sizeof(name),
                                 expr,
                                 sizeof(expr))) {
        if (v3_call_expr_string_literal_count(expr) > *max_string_literal_temps_io) {
            *max_string_literal_temps_io = v3_call_expr_string_literal_count(expr);
        }
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          expr,
                                          0,
                                          max_call_depth_io);
    }
    if (strchr(statement, '(') != NULL) {
        if (v3_call_expr_string_literal_count(statement) > *max_string_literal_temps_io) {
            *max_string_literal_temps_io = v3_call_expr_string_literal_count(statement);
        }
        return v3_prepare_expr_call_state(plan,
                                          lowering,
                                          function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          local_cap,
                                          next_offset_io,
                                          statement,
                                          0,
                                          max_call_depth_io);
    }
    return false;
}

static bool v3_prepare_if_statement_state(const V3SystemLinkPlanStub *plan,
                                          const V3LoweringPlanStub *lowering,
                                          const V3LoweredFunctionStub *function,
                                          const V3ImportAlias *aliases,
                                          size_t alias_count,
                                          V3AsmLocalSlot *locals,
                                          size_t *local_count,
                                          size_t local_cap,
                                          int32_t *next_offset_io,
                                          char **lines,
                                          size_t line_count,
                                          size_t *index_io,
                                          int32_t parent_indent,
                                          const char *statement,
                                          int32_t *max_call_depth_io,
                                          int32_t *max_string_literal_temps_io) {
    char current_stmt[8192];
    snprintf(current_stmt, sizeof(current_stmt), "%s", statement);
    for (;;) {
        char cond[4096];
        char inline_stmt[8192];
        char next_clause[8192];
        bool is_else = false;
        bool has_next_clause = false;
        if (v3_startswith(current_stmt, "if ")) {
            if (!v3_parse_if_header(current_stmt, cond, sizeof(cond), inline_stmt, sizeof(inline_stmt))) {
                fprintf(stderr,
                        "[cheng_v3_seed] parse if header failed function=%s stmt=%s\n",
                        function->symbol_text,
                        current_stmt);
                return false;
            }
        } else if (v3_startswith(current_stmt, "elif ")) {
            if (!v3_parse_elif_header(current_stmt, cond, sizeof(cond), inline_stmt, sizeof(inline_stmt))) {
                fprintf(stderr,
                        "[cheng_v3_seed] parse elif header failed function=%s stmt=%s\n",
                        function->symbol_text,
                        current_stmt);
                return false;
            }
        } else {
            is_else = true;
            if (!v3_parse_else_header(current_stmt, inline_stmt, sizeof(inline_stmt))) {
                fprintf(stderr,
                        "[cheng_v3_seed] parse else header failed function=%s stmt=%s\n",
                        function->symbol_text,
                        current_stmt);
                return false;
            }
            cond[0] = '\0';
        }
        if (!is_else) {
            if (v3_call_expr_string_literal_count(cond) > *max_string_literal_temps_io) {
                *max_string_literal_temps_io = v3_call_expr_string_literal_count(cond);
            }
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            local_cap,
                                            next_offset_io,
                                            cond,
                                            0,
                                            max_call_depth_io)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare if cond failed function=%s cond=%s\n",
                        function->symbol_text,
                        cond);
                return false;
            }
        }
        if (inline_stmt[0] != '\0') {
            if (v3_startswith(inline_stmt, "if ") ||
                v3_startswith(inline_stmt, "while ") ||
                v3_startswith(inline_stmt, "for ") ||
                v3_startswith(inline_stmt, "case ")) {
                return false;
            }
            if (!v3_prepare_non_if_statement_state(plan,
                                                   lowering,
                                                   function,
                                                   aliases,
                                                   alias_count,
                                                   locals,
                                                   local_count,
                                                   local_cap,
                                                   next_offset_io,
                                                   inline_stmt,
                                                   max_call_depth_io,
                                                   max_string_literal_temps_io)) {
                return false;
            }
        } else {
            while (*index_io < line_count) {
                char raw_copy[4096];
                char *trimmed;
                int32_t indent;
                char nested_stmt[8192];
                snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
                trimmed = v3_trim_inplace(raw_copy);
                if (*trimmed == '\0') {
                    *index_io += 1U;
                    continue;
                }
                indent = v3_line_indent(lines[*index_io]);
                if (indent < parent_indent) {
                    break;
                }
                if (indent == parent_indent &&
                    (v3_startswith(trimmed, "elif ") || v3_startswith(trimmed, "else"))) {
                    if (!v3_collect_statement_from_lines(lines,
                                                         line_count,
                                                         index_io,
                                                         next_clause,
                                                         sizeof(next_clause))) {
                        return false;
                    }
                    has_next_clause = true;
                    break;
                }
                if (indent <= parent_indent) {
                    break;
                }
                if (!v3_collect_statement_from_lines(lines, line_count, index_io, nested_stmt, sizeof(nested_stmt))) {
                    return false;
                }
                if (v3_startswith(nested_stmt, "if ")) {
                    if (!v3_prepare_if_statement_state(plan,
                                                       lowering,
                                                       function,
                                                       aliases,
                                                       alias_count,
                                                       locals,
                                                       local_count,
                                                       local_cap,
                                                       next_offset_io,
                                                       lines,
                                                       line_count,
                                                       index_io,
                                                       indent,
                                                       nested_stmt,
                                                       max_call_depth_io,
                                                       max_string_literal_temps_io)) {
                        return false;
                    }
                    continue;
                }
                if (v3_startswith(nested_stmt, "while ")) {
                    if (!v3_prepare_while_statement_state(plan,
                                                          lowering,
                                                          function,
                                                          aliases,
                                                          alias_count,
                                                          locals,
                                                          local_count,
                                                          local_cap,
                                                          next_offset_io,
                                                          lines,
                                                          line_count,
                                                          index_io,
                                                          indent,
                                                          nested_stmt,
                                                          max_call_depth_io,
                                                          max_string_literal_temps_io)) {
                        return false;
                    }
                    continue;
                }
                if (v3_startswith(nested_stmt, "for ")) {
                    if (!v3_prepare_for_statement_state(plan,
                                                        lowering,
                                                        function,
                                                        aliases,
                                                        alias_count,
                                                        locals,
                                                        local_count,
                                                        local_cap,
                                                        next_offset_io,
                                                        lines,
                                                        line_count,
                                                        index_io,
                                                        indent,
                                                        nested_stmt,
                                                        max_call_depth_io,
                                                        max_string_literal_temps_io)) {
                        return false;
                    }
                    continue;
                }
                if (!v3_prepare_non_if_statement_state(plan,
                                                       lowering,
                                                       function,
                                                       aliases,
                                                       alias_count,
                                                       locals,
                                                       local_count,
                                                       local_cap,
                                                       next_offset_io,
                                                       nested_stmt,
                                                       max_call_depth_io,
                                                       max_string_literal_temps_io)) {
                    fprintf(stderr,
                            "[cheng_v3_seed] prepare if nested stmt failed function=%s parent=%s nested=%s\n",
                            function->symbol_text,
                            current_stmt,
                            nested_stmt);
                    return false;
                }
            }
        }
        if (!has_next_clause) {
            break;
        }
        snprintf(current_stmt, sizeof(current_stmt), "%s", next_clause);
    }
    return true;
}

static bool v3_prepare_case_const_table_statement(const V3SystemLinkPlanStub *plan,
                                                  const V3LoweringPlanStub *lowering,
                                                  const V3LoweredFunctionStub *function,
                                                  const V3ImportAlias *aliases,
                                                  size_t alias_count,
                                                  V3AsmLocalSlot *locals,
                                                  size_t *local_count,
                                                  size_t local_cap,
                                                  int32_t *next_offset_io,
                                                  char **lines,
                                                  size_t line_count,
                                                  size_t *index_io,
                                                  int32_t parent_indent,
                                                  const char *statement,
                                                  int32_t *max_call_depth_io,
                                                  int32_t *max_string_literal_temps_io) {
    char case_expr[4096];
    if (!v3_parse_case_header(statement, case_expr, sizeof(case_expr)) ||
        !v3_prepare_expr_call_state(plan,
                                    lowering,
                                    function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    local_cap,
                                    next_offset_io,
                                    case_expr,
                                    0,
                                    max_call_depth_io)) {
        return false;
    }
    while (*index_io < line_count) {
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        char arm_stmt[8192];
        bool is_else = false;
        int64_t match_value = 0;
        char inline_stmt[8192];
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            *index_io += 1U;
            continue;
        }
        indent = v3_line_indent(lines[*index_io]);
        if (indent < parent_indent) {
            break;
        }
        if (indent == parent_indent &&
            !v3_startswith(trimmed, "of ") &&
            !v3_startswith(trimmed, "else")) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, index_io, arm_stmt, sizeof(arm_stmt)) ||
            !v3_parse_case_arm_header(arm_stmt,
                                      &is_else,
                                      &match_value,
                                      inline_stmt,
                                      sizeof(inline_stmt))) {
            return false;
        }
        if (v3_startswith(inline_stmt, "if ") ||
            v3_startswith(inline_stmt, "while ") ||
            v3_startswith(inline_stmt, "for ") ||
            v3_startswith(inline_stmt, "case ")) {
            return false;
        }
        if (v3_call_expr_string_literal_count(inline_stmt) > *max_string_literal_temps_io) {
            *max_string_literal_temps_io = v3_call_expr_string_literal_count(inline_stmt);
        }
        if (!v3_prepare_non_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               local_cap,
                                               next_offset_io,
                                               inline_stmt,
                                               max_call_depth_io,
                                               max_string_literal_temps_io)) {
            return false;
        }
    }
    return true;
}

static bool v3_prepare_while_statement_state(const V3SystemLinkPlanStub *plan,
                                             const V3LoweringPlanStub *lowering,
                                             const V3LoweredFunctionStub *function,
                                             const V3ImportAlias *aliases,
                                             size_t alias_count,
                                             V3AsmLocalSlot *locals,
                                             size_t *local_count,
                                             size_t local_cap,
                                             int32_t *next_offset_io,
                                             char **lines,
                                             size_t line_count,
                                             size_t *index_io,
                                             int32_t parent_indent,
                                             const char *statement,
                                             int32_t *max_call_depth_io,
                                             int32_t *max_string_literal_temps_io) {
    char cond[4096];
    char inline_stmt[8192];
    if (!v3_parse_while_header(statement, cond, sizeof(cond), inline_stmt, sizeof(inline_stmt))) {
        fprintf(stderr,
                "[cheng_v3_seed] parse while header failed function=%s stmt=%s\n",
                function->symbol_text,
                statement);
        return false;
    }
    if (v3_call_expr_string_literal_count(cond) > *max_string_literal_temps_io) {
        *max_string_literal_temps_io = v3_call_expr_string_literal_count(cond);
    }
    if (!v3_prepare_expr_call_state(plan,
                                    lowering,
                                    function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    local_cap,
                                    next_offset_io,
                                    cond,
                                    0,
                                    max_call_depth_io)) {
        fprintf(stderr,
                "[cheng_v3_seed] prepare while cond failed function=%s cond=%s\n",
                function->symbol_text,
                cond);
        return false;
    }
    if (inline_stmt[0] != '\0') {
        if (v3_startswith(inline_stmt, "if ") ||
            v3_startswith(inline_stmt, "while ") ||
            v3_startswith(inline_stmt, "for ")) {
            return false;
        }
        if (!v3_prepare_non_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               local_cap,
                                               next_offset_io,
                                               inline_stmt,
                                               max_call_depth_io,
                                               max_string_literal_temps_io)) {
            fprintf(stderr,
                    "[cheng_v3_seed] prepare while inline stmt failed function=%s stmt=%s inline=%s\n",
                    function->symbol_text,
                    statement,
                    inline_stmt);
            return false;
        }
        return true;
    }
    while (*index_io < line_count) {
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        char nested_stmt[8192];
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            *index_io += 1U;
            continue;
        }
        indent = v3_line_indent(lines[*index_io]);
        if (indent <= parent_indent) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, index_io, nested_stmt, sizeof(nested_stmt))) {
            return false;
        }
        if (v3_startswith(nested_stmt, "if ")) {
            if (!v3_prepare_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               local_cap,
                                               next_offset_io,
                                               lines,
                                               line_count,
                                               index_io,
                                               indent,
                                               nested_stmt,
                                               max_call_depth_io,
                                               max_string_literal_temps_io)) {
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "while ")) {
            if (!v3_prepare_while_statement_state(plan,
                                                  lowering,
                                                  function,
                                                  aliases,
                                                  alias_count,
                                                  locals,
                                                  local_count,
                                                  local_cap,
                                                  next_offset_io,
                                                  lines,
                                                  line_count,
                                                  index_io,
                                                  indent,
                                                  nested_stmt,
                                                  max_call_depth_io,
                                                  max_string_literal_temps_io)) {
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "for ")) {
            if (!v3_prepare_for_statement_state(plan,
                                                lowering,
                                                function,
                                                aliases,
                                                alias_count,
                                                locals,
                                                local_count,
                                                local_cap,
                                                next_offset_io,
                                                lines,
                                                line_count,
                                                index_io,
                                                indent,
                                                nested_stmt,
                                                max_call_depth_io,
                                                max_string_literal_temps_io)) {
                return false;
            }
            continue;
        }
        if (!v3_prepare_non_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               local_cap,
                                               next_offset_io,
                                               nested_stmt,
                                               max_call_depth_io,
                                               max_string_literal_temps_io)) {
            fprintf(stderr,
                    "[cheng_v3_seed] prepare while nested stmt failed function=%s parent=%s nested=%s\n",
                    function->symbol_text,
                    statement,
                    nested_stmt);
            return false;
        }
    }
    return true;
}

static bool v3_prepare_for_statement_state(const V3SystemLinkPlanStub *plan,
                                           const V3LoweringPlanStub *lowering,
                                           const V3LoweredFunctionStub *function,
                                           const V3ImportAlias *aliases,
                                           size_t alias_count,
                                           V3AsmLocalSlot *locals,
                                           size_t *local_count,
                                           size_t local_cap,
                                           int32_t *next_offset_io,
                                           char **lines,
                                           size_t line_count,
                                           size_t *index_io,
                                           int32_t parent_indent,
                                           const char *statement,
                                           int32_t *max_call_depth_io,
                                           int32_t *max_string_literal_temps_io) {
    char iter_name[128];
    char start_expr[4096];
    char end_expr[4096];
    char inline_stmt[8192];
    if (!v3_parse_for_header(statement,
                             iter_name,
                             sizeof(iter_name),
                             start_expr,
                             sizeof(start_expr),
                             end_expr,
                             sizeof(end_expr),
                             inline_stmt,
                             sizeof(inline_stmt))) {
        fprintf(stderr,
                "[cheng_v3_seed] parse for header failed function=%s stmt=%s\n",
                function->symbol_text,
                statement);
        return false;
    }
    if (v3_find_local_slot(locals, *local_count, iter_name) < 0 &&
        !v3_add_local_slot_for_type(lowering,
                                    function->owner_module_path,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    local_cap,
                                    iter_name,
                                    "int32",
                                    "i32",
                                    false,
                                    next_offset_io)) {
        return false;
    }
    if (v3_call_expr_string_literal_count(start_expr) > *max_string_literal_temps_io) {
        *max_string_literal_temps_io = v3_call_expr_string_literal_count(start_expr);
    }
    if (v3_call_expr_string_literal_count(end_expr) > *max_string_literal_temps_io) {
        *max_string_literal_temps_io = v3_call_expr_string_literal_count(end_expr);
    }
    if (!v3_prepare_expr_call_state(plan,
                                    lowering,
                                    function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    local_cap,
                                    next_offset_io,
                                    start_expr,
                                    0,
                                    max_call_depth_io) ||
        !v3_prepare_expr_call_state(plan,
                                    lowering,
                                    function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    local_cap,
                                    next_offset_io,
                                    end_expr,
                                    0,
                                    max_call_depth_io)) {
        fprintf(stderr,
                "[cheng_v3_seed] prepare for range failed function=%s start=%s end=%s\n",
                function->symbol_text,
                start_expr,
                end_expr);
        return false;
    }
    if (inline_stmt[0] != '\0') {
        if (v3_startswith(inline_stmt, "if ") ||
            v3_startswith(inline_stmt, "while ") ||
            v3_startswith(inline_stmt, "for ")) {
            return false;
        }
        if (!v3_prepare_non_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               local_cap,
                                               next_offset_io,
                                               inline_stmt,
                                               max_call_depth_io,
                                               max_string_literal_temps_io)) {
            fprintf(stderr,
                    "[cheng_v3_seed] prepare for inline stmt failed function=%s stmt=%s inline=%s\n",
                    function->symbol_text,
                    statement,
                    inline_stmt);
            return false;
        }
        return true;
    }
    while (*index_io < line_count) {
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        char nested_stmt[8192];
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            *index_io += 1U;
            continue;
        }
        indent = v3_line_indent(lines[*index_io]);
        if (indent <= parent_indent) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, index_io, nested_stmt, sizeof(nested_stmt))) {
            return false;
        }
        if (v3_startswith(nested_stmt, "if ")) {
            if (!v3_prepare_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               local_cap,
                                               next_offset_io,
                                               lines,
                                               line_count,
                                               index_io,
                                               indent,
                                               nested_stmt,
                                               max_call_depth_io,
                                               max_string_literal_temps_io)) {
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "while ")) {
            if (!v3_prepare_while_statement_state(plan,
                                                  lowering,
                                                  function,
                                                  aliases,
                                                  alias_count,
                                                  locals,
                                                  local_count,
                                                  local_cap,
                                                  next_offset_io,
                                                  lines,
                                                  line_count,
                                                  index_io,
                                                  indent,
                                                  nested_stmt,
                                                  max_call_depth_io,
                                                  max_string_literal_temps_io)) {
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "for ")) {
            if (!v3_prepare_for_statement_state(plan,
                                                lowering,
                                                function,
                                                aliases,
                                                alias_count,
                                                locals,
                                                local_count,
                                                local_cap,
                                                next_offset_io,
                                                lines,
                                                line_count,
                                                index_io,
                                                indent,
                                                nested_stmt,
                                                max_call_depth_io,
                                                max_string_literal_temps_io)) {
                return false;
            }
            continue;
        }
        if (!v3_prepare_non_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               local_cap,
                                               next_offset_io,
                                               nested_stmt,
                                               max_call_depth_io,
                                               max_string_literal_temps_io)) {
            fprintf(stderr,
                    "[cheng_v3_seed] prepare for nested stmt failed function=%s parent=%s nested=%s\n",
                    function->symbol_text,
                    statement,
                    nested_stmt);
            return false;
        }
    }
    return true;
}

static bool v3_emit_if_statement(const V3SystemLinkPlanStub *plan,
                                 const V3LoweringPlanStub *lowering,
                                 const V3LoweredFunctionStub *function,
                                 const V3ImportAlias *aliases,
                                 size_t alias_count,
                                 const V3AsmLocalSlot *locals,
                                 size_t local_count,
                                 char **lines,
                                 size_t line_count,
                                 size_t *index_io,
                                 int32_t parent_indent,
                                 const char *statement,
                                 int32_t call_arg_base,
                                 int32_t string_temp_base,
                                 int32_t string_temp_stride,
                                 int32_t *string_label_index_io,
                                 int32_t *control_label_index_io,
                                 int32_t frame_size,
                                 int32_t sret_local_index,
                                 char *out,
                                 size_t cap);
static bool v3_emit_while_statement(const V3SystemLinkPlanStub *plan,
                                    const V3LoweringPlanStub *lowering,
                                    const V3LoweredFunctionStub *function,
                                    const V3ImportAlias *aliases,
                                    size_t alias_count,
                                    const V3AsmLocalSlot *locals,
                                    size_t local_count,
                                    char **lines,
                                    size_t line_count,
                                    size_t *index_io,
                                    int32_t parent_indent,
                                    const char *statement,
                                    int32_t call_arg_base,
                                    int32_t string_temp_base,
                                    int32_t string_temp_stride,
                                    int32_t *string_label_index_io,
                                    int32_t *control_label_index_io,
                                    int32_t frame_size,
                                    int32_t sret_local_index,
                                    char *out,
                                    size_t cap);
static bool v3_emit_for_statement(const V3SystemLinkPlanStub *plan,
                                  const V3LoweringPlanStub *lowering,
                                  const V3LoweredFunctionStub *function,
                                  const V3ImportAlias *aliases,
                                  size_t alias_count,
                                  const V3AsmLocalSlot *locals,
                                  size_t local_count,
                                  char **lines,
                                  size_t line_count,
                                  size_t *index_io,
                                  int32_t parent_indent,
                                  const char *statement,
                                  int32_t call_arg_base,
                                  int32_t string_temp_base,
                                  int32_t string_temp_stride,
                                  int32_t *string_label_index_io,
                                  int32_t *control_label_index_io,
                                  int32_t frame_size,
                                  int32_t sret_local_index,
                                  char *out,
                                  size_t cap);
static bool v3_emit_case_const_table_statement(const V3SystemLinkPlanStub *plan,
                                               const V3LoweringPlanStub *lowering,
                                               const V3LoweredFunctionStub *function,
                                               const V3ImportAlias *aliases,
                                               size_t alias_count,
                                               const V3AsmLocalSlot *locals,
                                               size_t local_count,
                                               char **lines,
                                               size_t line_count,
                                               size_t *index_io,
                                               int32_t parent_indent,
                                               const char *statement,
                                               int32_t call_arg_base,
                                               int32_t string_temp_base,
                                               int32_t string_temp_stride,
                                               int32_t *string_label_index_io,
                                               int32_t *control_label_index_io,
                                               int32_t frame_size,
                                               int32_t sret_local_index,
                                               bool *terminated_out,
                                               char *out,
                                               size_t cap);

static bool v3_emit_non_if_statement(const V3SystemLinkPlanStub *plan,
                                     const V3LoweringPlanStub *lowering,
                                     const V3LoweredFunctionStub *function,
                                     const V3ImportAlias *aliases,
                                     size_t alias_count,
                                     const V3AsmLocalSlot *locals,
                                     size_t local_count,
                                     const char *statement,
                                     int32_t call_arg_base,
                                     int32_t string_temp_base,
                                     int32_t string_temp_stride,
                                     int32_t *string_label_index_io,
                                     int32_t *control_label_index_io,
                                     int32_t frame_size,
                                     int32_t sret_local_index,
                                     char *out,
                                     size_t cap,
                                     bool *terminated_out) {
    char name[128];
    char type_text[128];
    char expr[4096];
    *terminated_out = false;
    if (strcmp(statement, "continue") == 0) {
        const char *label = v3_current_continue_label();
        if (!label) {
            return false;
        }
        v3_text_appendf(out, cap, "  b %s\n", label);
        *terminated_out = true;
        return true;
    }
    if (strcmp(statement, "break") == 0) {
        const char *label = v3_current_break_label();
        if (!label) {
            return false;
        }
        v3_text_appendf(out, cap, "  b %s\n", label);
        *terminated_out = true;
        return true;
    }
    if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
        v3_parse_binding_decl_meta(statement,
                                   v3_startswith(statement, "let ") ? "let " : "var ",
                                   name,
                                   sizeof(name),
                                   type_text,
                                   sizeof(type_text))) {
        int32_t local_index = v3_find_local_slot(locals, local_count, name);
        return local_index >= 0 && v3_emit_zero_slot(out, cap, &locals[local_index]);
    }
    if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
        v3_parse_binding_meta(statement,
                              v3_startswith(statement, "let ") ? "let " : "var ",
                              name,
                              sizeof(name),
                              type_text,
                              sizeof(type_text),
                              expr,
                              sizeof(expr))) {
        int32_t local_index = v3_find_local_slot(locals, local_count, name);
        const char *abi_class;
        char inferred_type[128];
        char inferred_abi[32];
        if (type_text[0] == '\0') {
            if (!v3_infer_expr_type(plan,
                                    lowering,
                                    function,
                                    aliases,
                                    alias_count,
                                    locals,
                                    local_count,
                                    expr,
                                    inferred_type,
                                    sizeof(inferred_type),
                                    inferred_abi,
                                    sizeof(inferred_abi))) {
                return false;
            }
            snprintf(type_text, sizeof(type_text), "%s", inferred_type);
        }
        abi_class = v3_type_abi_class(type_text);
        if (local_index < 0 || locals[local_index].stack_offset < 0) {
            return false;
        }
        if (v3_abi_class_scalar_or_ptr(abi_class)) {
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        expr,
                                        abi_class,
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        0,
                                        out,
                                        cap)) {
                return false;
            }
            v3_emit_store_slot(out, cap, &locals[local_index], 9);
            return true;
        }
        return v3_materialize_composite_expr_into_slot(plan,
                                                       lowering,
                                                       function,
                                                       aliases,
                                                       alias_count,
                                                       locals,
                                                       local_count,
                                                       name,
                                                       0U,
                                                       expr,
                                                       &locals[local_index],
                                                       call_arg_base,
                                                       string_temp_base,
                                                       string_temp_stride,
                                                       string_label_index_io,
                                                       0,
                                                       out,
                                                       cap);
    }
    if (v3_parse_assignment_meta(statement,
                                 name,
                                 sizeof(name),
                                 expr,
                                 sizeof(expr))) {
        int32_t local_index = v3_find_local_slot(locals, local_count, name);
        char field_type[256];
        char field_abi[32];
        if (local_index >= 0) {
            const char *abi_class = locals[local_index].abi_class;
            if (v3_abi_class_scalar_or_ptr(abi_class)) {
                if (!v3_codegen_expr_scalar(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            expr,
                                            abi_class,
                                            9,
                                            call_arg_base,
                                            string_temp_base,
                                            string_temp_stride,
                                            0,
                                            out,
                                            cap)) {
                    return false;
                }
                v3_emit_store_slot(out, cap, &locals[local_index], 9);
                return true;
            }
            return v3_materialize_composite_expr_into_slot(plan,
                                                           lowering,
                                                           function,
                                                           aliases,
                                                           alias_count,
                                                           locals,
                                                           local_count,
                                                           name,
                                                           0U,
                                                           expr,
                                                           &locals[local_index],
                                                           call_arg_base,
                                                           string_temp_base,
                                                           string_temp_stride,
                                                           string_label_index_io,
                                                           0,
                                                           out,
                                                           cap);
        }
        if (v3_infer_lvalue_expr_type(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      name,
                                      field_type,
                                      sizeof(field_type),
                                      field_abi,
                                      sizeof(field_abi))) {
            if (v3_abi_class_scalar_or_ptr(field_abi)) {
                if (!v3_codegen_expr_scalar(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            expr,
                                            field_abi,
                                            9,
                                            call_arg_base,
                                            string_temp_base,
                                            string_temp_stride,
                                            0,
                                            out,
                                            cap)) {
                    return false;
                }
                if (!v3_emit_lvalue_address(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            name,
                                            field_type,
                                            sizeof(field_type),
                                            field_abi,
                                            sizeof(field_abi),
                                            15,
                                            call_arg_base,
                                            string_temp_base,
                                            string_temp_stride,
                                            0,
                                            out,
                                            cap)) {
                    return false;
                }
                v3_emit_store_scalar_to_address(out, cap, 15, 0, field_abi, 9);
                return true;
            }
            if (!v3_emit_lvalue_address(plan,
                                        lowering,
                                        function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        name,
                                        field_type,
                                        sizeof(field_type),
                                        field_abi,
                                        sizeof(field_abi),
                                        15,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        0,
                                        out,
                                        cap)) {
                return false;
            }
            return v3_materialize_composite_expr_into_address(plan,
                                                              lowering,
                                                              function,
                                                              aliases,
                                                              alias_count,
                                                              locals,
                                                              local_count,
                                                              expr,
                                                              field_type,
                                                              15,
                                                              call_arg_base,
                                                              string_temp_base,
                                                              string_temp_stride,
                                                              string_label_index_io,
                                                              0,
                                                              out,
                                                              cap);
        }
        return false;
    }
    if (strcmp(statement, "return") == 0) {
        if (strcmp(function->return_abi_class, "void") != 0) {
            return false;
        }
        if (!v3_emit_function_epilogue(out, cap, frame_size)) {
            return false;
        }
        *terminated_out = true;
        return true;
    }
    if (v3_startswith(statement, "return ")) {
        if (strcmp(function->return_abi_class, "composite") == 0) {
            if (sret_local_index < 0 ||
                !v3_emit_slot_address(out, cap, &locals[sret_local_index], 15) ||
                !v3_materialize_composite_expr_into_address(plan,
                                                            lowering,
                                                            function,
                                                            aliases,
                                                            alias_count,
                                                            locals,
                                                            local_count,
                                                            statement + 7,
                                                            function->return_type,
                                                            15,
                                                            call_arg_base,
                                                            string_temp_base,
                                                            string_temp_stride,
                                                            string_label_index_io,
                                                            0,
                                                            out,
                                                            cap)) {
                return false;
            }
        } else if (!v3_codegen_expr_scalar(plan,
                                           lowering,
                                           function,
                                           aliases,
                                           alias_count,
                                           locals,
                                           local_count,
                                           statement + 7,
                                           function->return_abi_class,
                                           9,
                                           call_arg_base,
                                           string_temp_base,
                                           string_temp_stride,
                                           0,
                                           out,
                                           cap)) {
            return false;
        } else {
            v3_emit_reg_to_return(out, cap, 9, function->return_abi_class);
        }
        if (!v3_emit_function_epilogue(out, cap, frame_size)) {
            return false;
        }
        *terminated_out = true;
        return true;
    }
    if (strchr(statement, '(') != NULL) {
        bool handled = false;
        char callee[PATH_MAX];
        char args[CHENG_V3_MAX_CALL_ARGS][4096];
        size_t arg_count = 0U;
        callee[0] = '\0';
        v3_parse_call_text(statement, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS);
        if (!v3_try_emit_builtin_statement(plan,
                                           lowering,
                                           function,
                                           aliases,
                                           alias_count,
                                           locals,
                                           local_count,
                                           statement,
                                           call_arg_base,
                                           string_temp_base,
                                           string_temp_stride,
                                           string_label_index_io,
                                           control_label_index_io,
                                           out,
                                           cap,
                                           &handled)) {
            return false;
        }
        if (handled) {
            if (strcmp(callee, "panic") == 0 || strcmp(callee, "system.panic") == 0) {
                *terminated_out = true;
            }
            return true;
        }
        if (!v3_parse_call_text(statement, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS)) {
            return false;
        }
        return v3_codegen_call_scalar(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      callee,
                                      args,
                                      arg_count,
                                      "void",
                                      9,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      0,
                                      out,
                                      cap);
    }
    return false;
}

static bool v3_emit_if_statement(const V3SystemLinkPlanStub *plan,
                                 const V3LoweringPlanStub *lowering,
                                 const V3LoweredFunctionStub *function,
                                 const V3ImportAlias *aliases,
                                 size_t alias_count,
                                 const V3AsmLocalSlot *locals,
                                 size_t local_count,
                                 char **lines,
                                 size_t line_count,
                                 size_t *index_io,
                                 int32_t parent_indent,
                                 const char *statement,
                                 int32_t call_arg_base,
                                 int32_t string_temp_base,
                                 int32_t string_temp_stride,
                                 int32_t *string_label_index_io,
                                 int32_t *control_label_index_io,
                                 int32_t frame_size,
                                 int32_t sret_local_index,
                                 char *out,
                                 size_t cap) {
    char current_stmt[8192];
    char end_label[128];
    int32_t chain_id = v3_next_expr_label_id();
    snprintf(end_label, sizeof(end_label), "L_v3_if_end_%d", chain_id);
    snprintf(current_stmt, sizeof(current_stmt), "%s", statement);
    for (;;) {
        char cond[4096];
        char inline_stmt[8192];
        char next_clause[8192];
        char next_label[128];
        bool is_else = false;
        bool has_next_clause = false;
        bool branch_terminated = false;
        bool arm_has_cond = false;
        if (v3_startswith(current_stmt, "if ")) {
            arm_has_cond = true;
            if (!v3_parse_if_header(current_stmt, cond, sizeof(cond), inline_stmt, sizeof(inline_stmt))) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit if parse header failed function=%s stmt=%s\n",
                        function->symbol_text,
                        current_stmt);
                return false;
            }
        } else if (v3_startswith(current_stmt, "elif ")) {
            arm_has_cond = true;
            if (!v3_parse_elif_header(current_stmt, cond, sizeof(cond), inline_stmt, sizeof(inline_stmt))) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit elif parse header failed function=%s stmt=%s\n",
                        function->symbol_text,
                        current_stmt);
                return false;
            }
        } else {
            is_else = true;
            if (!v3_parse_else_header(current_stmt, inline_stmt, sizeof(inline_stmt))) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit else parse header failed function=%s stmt=%s\n",
                        function->symbol_text,
                        current_stmt);
                return false;
            }
            cond[0] = '\0';
        }
        if (arm_has_cond) {
            int32_t next_id = v3_next_expr_label_id();
            snprintf(next_label, sizeof(next_label), "L_v3_if_next_%d", next_id);
            if (!v3_codegen_expr_scalar(plan,
                                        lowering,
                                        function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        cond,
                                        "bool",
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        0,
                                        out,
                                        cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit if cond failed function=%s cond=%s\n",
                        function->symbol_text,
                        cond);
                return false;
            }
            v3_text_appendf(out, cap,
                            "  cmp w9, #0\n"
                            "  b.eq %s\n",
                            next_label);
        } else {
            next_label[0] = '\0';
        }
        if (inline_stmt[0] != '\0') {
            if (v3_startswith(inline_stmt, "if ") ||
                v3_startswith(inline_stmt, "while ") ||
                v3_startswith(inline_stmt, "for ") ||
                v3_startswith(inline_stmt, "case ")) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit if inline compound reject function=%s inline=%s\n",
                        function->symbol_text,
                        inline_stmt);
                return false;
            }
            if (!v3_emit_non_if_statement(plan,
                                          lowering,
                                          function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          inline_stmt,
                                          call_arg_base,
                                          string_temp_base,
                                          string_temp_stride,
                                          string_label_index_io,
                                          control_label_index_io,
                                          frame_size,
                                          sret_local_index,
                                          out,
                                          cap,
                                          &branch_terminated)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit if inline stmt failed function=%s inline=%s\n",
                        function->symbol_text,
                        inline_stmt);
                return false;
            }
        } else {
            while (*index_io < line_count) {
                char raw_copy[4096];
                char *trimmed;
                int32_t indent;
                char nested_stmt[8192];
                snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
                trimmed = v3_trim_inplace(raw_copy);
                if (*trimmed == '\0') {
                    *index_io += 1U;
                    continue;
                }
                indent = v3_line_indent(lines[*index_io]);
                if (indent < parent_indent) {
                    break;
                }
                if (indent == parent_indent &&
                    (v3_startswith(trimmed, "elif ") || v3_startswith(trimmed, "else"))) {
                    if (!v3_collect_statement_from_lines(lines,
                                                         line_count,
                                                         index_io,
                                                         next_clause,
                                                         sizeof(next_clause))) {
                        fprintf(stderr,
                                "[cheng_v3_seed] emit if collect next clause failed function=%s stmt=%s\n",
                                function->symbol_text,
                                current_stmt);
                        return false;
                    }
                    has_next_clause = true;
                    break;
                }
                if (indent <= parent_indent) {
                    break;
                }
                if (branch_terminated) {
                    *index_io += 1U;
                    continue;
                }
                if (!v3_collect_statement_from_lines(lines, line_count, index_io, nested_stmt, sizeof(nested_stmt))) {
                    fprintf(stderr,
                            "[cheng_v3_seed] emit if collect nested stmt failed function=%s parent=%s\n",
                            function->symbol_text,
                            current_stmt);
                    return false;
                }
                if (v3_startswith(nested_stmt, "if ")) {
                    if (!v3_emit_if_statement(plan,
                                              lowering,
                                              function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              lines,
                                              line_count,
                                              index_io,
                                              indent,
                                              nested_stmt,
                                              call_arg_base,
                                              string_temp_base,
                                              string_temp_stride,
                                              string_label_index_io,
                                              control_label_index_io,
                                              frame_size,
                                              sret_local_index,
                                              out,
                                              cap)) {
                        fprintf(stderr,
                                "[cheng_v3_seed] emit nested if failed function=%s parent=%s nested=%s\n",
                                function->symbol_text,
                                current_stmt,
                                nested_stmt);
                        return false;
                    }
                    continue;
                }
                if (v3_startswith(nested_stmt, "while ")) {
                    if (!v3_emit_while_statement(plan,
                                                 lowering,
                                                 function,
                                                 aliases,
                                                 alias_count,
                                                 locals,
                                                 local_count,
                                                 lines,
                                                 line_count,
                                                 index_io,
                                                 indent,
                                                 nested_stmt,
                                                 call_arg_base,
                                                 string_temp_base,
                                                 string_temp_stride,
                                                 string_label_index_io,
                                                 control_label_index_io,
                                                 frame_size,
                                                 sret_local_index,
                                                 out,
                                                 cap)) {
                        fprintf(stderr,
                                "[cheng_v3_seed] emit nested while failed function=%s parent=%s nested=%s\n",
                                function->symbol_text,
                                current_stmt,
                                nested_stmt);
                        return false;
                    }
                    continue;
                }
                if (v3_startswith(nested_stmt, "for ")) {
                    if (!v3_emit_for_statement(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               lines,
                                               line_count,
                                               index_io,
                                               indent,
                                               nested_stmt,
                                               call_arg_base,
                                               string_temp_base,
                                               string_temp_stride,
                                               string_label_index_io,
                                               control_label_index_io,
                                               frame_size,
                                               sret_local_index,
                                               out,
                                               cap)) {
                        fprintf(stderr,
                                "[cheng_v3_seed] emit nested for failed function=%s parent=%s nested=%s\n",
                                function->symbol_text,
                                current_stmt,
                                nested_stmt);
                        return false;
                    }
                    continue;
                }
                if (!v3_emit_non_if_statement(plan,
                                              lowering,
                                              function,
                                              aliases,
                                              alias_count,
                                              locals,
                                              local_count,
                                              nested_stmt,
                                              call_arg_base,
                                              string_temp_base,
                                              string_temp_stride,
                                              string_label_index_io,
                                              control_label_index_io,
                                              frame_size,
                                              sret_local_index,
                                              out,
                                              cap,
                                              &branch_terminated)) {
                    fprintf(stderr,
                            "[cheng_v3_seed] emit if nested stmt failed function=%s parent=%s nested=%s\n",
                            function->symbol_text,
                            current_stmt,
                            nested_stmt);
                    return false;
                }
            }
        }
        if (!is_else && !branch_terminated) {
            v3_text_appendf(out, cap, "  b %s\n", end_label);
        }
        if (!is_else) {
            v3_text_appendf(out, cap, "%s:\n", next_label);
        }
        if (!has_next_clause) {
            break;
        }
        snprintf(current_stmt, sizeof(current_stmt), "%s", next_clause);
    }
    v3_text_appendf(out, cap, "%s:\n", end_label);
    return true;
}

static bool v3_emit_while_statement(const V3SystemLinkPlanStub *plan,
                                    const V3LoweringPlanStub *lowering,
                                    const V3LoweredFunctionStub *function,
                                    const V3ImportAlias *aliases,
                                    size_t alias_count,
                                    const V3AsmLocalSlot *locals,
                                    size_t local_count,
                                    char **lines,
                                    size_t line_count,
                                    size_t *index_io,
                                    int32_t parent_indent,
                                    const char *statement,
                                    int32_t call_arg_base,
                                    int32_t string_temp_base,
                                    int32_t string_temp_stride,
                                    int32_t *string_label_index_io,
                                    int32_t *control_label_index_io,
                                    int32_t frame_size,
                                    int32_t sret_local_index,
                                    char *out,
                                    size_t cap) {
    char cond[4096];
    char inline_stmt[8192];
    char start_label[128];
    char end_label[128];
    bool branch_terminated = false;
    int32_t label_id = v3_next_expr_label_id();
    if (!v3_parse_while_header(statement, cond, sizeof(cond), inline_stmt, sizeof(inline_stmt))) {
        return false;
    }
    snprintf(start_label, sizeof(start_label), "L_v3_while_start_%d", label_id);
    snprintf(end_label, sizeof(end_label), "L_v3_while_end_%d", label_id);
    if (!v3_push_loop_labels(start_label, end_label)) {
        return false;
    }
    v3_text_appendf(out, cap, "%s:\n", start_label);
    if (!v3_codegen_expr_scalar(plan,
                                lowering,
                                function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                cond,
                                "bool",
                                9,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                0,
                                out,
                                cap)) {
        return false;
    }
    v3_text_appendf(out, cap,
                    "  cmp w9, #0\n"
                    "  b.eq %s\n",
                    end_label);
    if (inline_stmt[0] != '\0') {
        if (v3_startswith(inline_stmt, "if ") ||
            v3_startswith(inline_stmt, "while ") ||
            v3_startswith(inline_stmt, "for ")) {
            return false;
        }
        if (!v3_emit_non_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      inline_stmt,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      control_label_index_io,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap,
                                      &branch_terminated)) {
            v3_pop_loop_labels();
            return false;
        }
        v3_text_appendf(out, cap, "  b %s\n", start_label);
        v3_text_appendf(out, cap, "%s:\n", end_label);
        v3_pop_loop_labels();
        return true;
    }
    while (*index_io < line_count) {
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        char nested_stmt[8192];
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            *index_io += 1U;
            continue;
        }
        indent = v3_line_indent(lines[*index_io]);
        if (indent <= parent_indent) {
            break;
        }
        if (branch_terminated) {
            *index_io += 1U;
            continue;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, index_io, nested_stmt, sizeof(nested_stmt))) {
            v3_pop_loop_labels();
            return false;
        }
        if (v3_startswith(nested_stmt, "if ")) {
            if (!v3_emit_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      lines,
                                      line_count,
                                      index_io,
                                      indent,
                                      nested_stmt,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      control_label_index_io,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap)) {
                v3_pop_loop_labels();
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "while ")) {
            if (!v3_emit_while_statement(plan,
                                         lowering,
                                         function,
                                         aliases,
                                         alias_count,
                                         locals,
                                         local_count,
                                         lines,
                                         line_count,
                                         index_io,
                                         indent,
                                         nested_stmt,
                                         call_arg_base,
                                         string_temp_base,
                                         string_temp_stride,
                                         string_label_index_io,
                                         control_label_index_io,
                                         frame_size,
                                         sret_local_index,
                                         out,
                                         cap)) {
                v3_pop_loop_labels();
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "for ")) {
            if (!v3_emit_for_statement(plan,
                                       lowering,
                                       function,
                                       aliases,
                                       alias_count,
                                       locals,
                                       local_count,
                                       lines,
                                       line_count,
                                       index_io,
                                       indent,
                                       nested_stmt,
                                       call_arg_base,
                                       string_temp_base,
                                       string_temp_stride,
                                       string_label_index_io,
                                       control_label_index_io,
                                       frame_size,
                                       sret_local_index,
                                       out,
                                       cap)) {
                v3_pop_loop_labels();
                return false;
            }
            continue;
        }
        if (!v3_emit_non_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      nested_stmt,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      control_label_index_io,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap,
                                      &branch_terminated)) {
            v3_pop_loop_labels();
            return false;
        }
    }
    v3_text_appendf(out, cap, "  b %s\n", start_label);
    v3_text_appendf(out, cap, "%s:\n", end_label);
    v3_pop_loop_labels();
    return true;
}

static bool v3_emit_case_const_table_statement(const V3SystemLinkPlanStub *plan,
                                               const V3LoweringPlanStub *lowering,
                                               const V3LoweredFunctionStub *function,
                                               const V3ImportAlias *aliases,
                                               size_t alias_count,
                                               const V3AsmLocalSlot *locals,
                                               size_t local_count,
                                               char **lines,
                                               size_t line_count,
                                               size_t *index_io,
                                               int32_t parent_indent,
                                               const char *statement,
                                               int32_t call_arg_base,
                                               int32_t string_temp_base,
                                               int32_t string_temp_stride,
                                               int32_t *string_label_index_io,
                                               int32_t *control_label_index_io,
                                               int32_t frame_size,
                                               int32_t sret_local_index,
                                               bool *terminated_out,
                                               char *out,
                                               size_t cap) {
    char case_expr[4096];
    char end_label[128];
    bool saw_else = false;
    bool all_arms_terminated = true;
    int32_t end_id = v3_next_expr_label_id();
    if (!v3_parse_case_header(statement, case_expr, sizeof(case_expr)) ||
        !v3_codegen_expr_scalar(plan,
                                lowering,
                                function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                case_expr,
                                "i64",
                                9,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                0,
                                out,
                                cap)) {
        fprintf(stderr,
                "[cheng_v3_seed] emit case selector failed function=%s stmt=%s expr=%s\n",
                function->symbol_text,
                statement,
                case_expr);
        return false;
    }
    snprintf(end_label, sizeof(end_label), "L_v3_case_end_%d", end_id);
    while (*index_io < line_count) {
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        char arm_stmt[8192];
        char inline_stmt[8192];
        char next_label[128];
        bool is_else = false;
        bool branch_terminated = false;
        int64_t match_value = 0;
        int32_t next_id = v3_next_expr_label_id();
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            *index_io += 1U;
            continue;
        }
        indent = v3_line_indent(lines[*index_io]);
        if (indent < parent_indent) {
            break;
        }
        if (indent == parent_indent &&
            !v3_startswith(trimmed, "of ") &&
            !v3_startswith(trimmed, "else")) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, index_io, arm_stmt, sizeof(arm_stmt)) ||
            !v3_parse_case_arm_header(arm_stmt,
                                      &is_else,
                                      &match_value,
                                      inline_stmt,
                                      sizeof(inline_stmt))) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit case arm parse failed function=%s stmt=%s\n",
                    function->symbol_text,
                    statement);
            return false;
        }
        if (!is_else) {
            snprintf(next_label, sizeof(next_label), "L_v3_case_next_%d", next_id);
            if (!v3_emit_mov_imm(out, cap, 10, match_value)) {
                return false;
            }
            v3_text_appendf(out, cap,
                            "  cmp x9, x10\n"
                            "  b.ne %s\n",
                            next_label);
        } else {
            saw_else = true;
            next_label[0] = '\0';
        }
        if (!v3_emit_non_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      inline_stmt,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      control_label_index_io,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap,
                                      &branch_terminated)) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit case arm body failed function=%s arm=%s inline=%s\n",
                    function->symbol_text,
                    arm_stmt,
                    inline_stmt);
            return false;
        }
        if (!branch_terminated) {
            all_arms_terminated = false;
        }
        if (!branch_terminated) {
            v3_text_appendf(out, cap, "  b %s\n", end_label);
        }
        if (!is_else) {
            v3_text_appendf(out, cap, "%s:\n", next_label);
        }
    }
    if (!saw_else) {
        fprintf(stderr,
                "[cheng_v3_seed] emit case missing else function=%s stmt=%s\n",
                function->symbol_text,
                statement);
        return false;
    }
    v3_text_appendf(out, cap, "%s:\n", end_label);
    if (terminated_out) {
        *terminated_out = saw_else && all_arms_terminated;
    }
    return true;
}

static bool v3_emit_for_statement(const V3SystemLinkPlanStub *plan,
                                  const V3LoweringPlanStub *lowering,
                                  const V3LoweredFunctionStub *function,
                                  const V3ImportAlias *aliases,
                                  size_t alias_count,
                                  const V3AsmLocalSlot *locals,
                                  size_t local_count,
                                  char **lines,
                                  size_t line_count,
                                  size_t *index_io,
                                  int32_t parent_indent,
                                  const char *statement,
                                  int32_t call_arg_base,
                                  int32_t string_temp_base,
                                  int32_t string_temp_stride,
                                  int32_t *string_label_index_io,
                                  int32_t *control_label_index_io,
                                  int32_t frame_size,
                                  int32_t sret_local_index,
                                  char *out,
                                  size_t cap) {
    char iter_name[128];
    char start_expr[4096];
    char end_expr[4096];
    char inline_stmt[8192];
    char start_label[128];
    char continue_label[128];
    char end_label[128];
    int32_t iter_local_index;
    bool branch_terminated = false;
    int32_t label_id = v3_next_expr_label_id();
    if (!v3_parse_for_header(statement,
                             iter_name,
                             sizeof(iter_name),
                             start_expr,
                             sizeof(start_expr),
                             end_expr,
                             sizeof(end_expr),
                             inline_stmt,
                             sizeof(inline_stmt))) {
        fprintf(stderr,
                "[cheng_v3_seed] emit for parse header failed function=%s stmt=%s\n",
                function->symbol_text,
                statement);
        return false;
    }
    iter_local_index = v3_find_local_slot(locals, local_count, iter_name);
    if (iter_local_index < 0) {
        return false;
    }
    if (!v3_codegen_expr_scalar(plan,
                                lowering,
                                function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                start_expr,
                                "i32",
                                9,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                0,
                                out,
                                cap)) {
        fprintf(stderr,
                "[cheng_v3_seed] emit for start expr failed function=%s stmt=%s start=%s\n",
                function->symbol_text,
                statement,
                start_expr);
        return false;
    }
    v3_emit_store_slot(out, cap, &locals[iter_local_index], 9);
    snprintf(start_label, sizeof(start_label), "L_v3_for_start_%d", label_id);
    snprintf(continue_label, sizeof(continue_label), "L_v3_for_continue_%d", label_id);
    snprintf(end_label, sizeof(end_label), "L_v3_for_end_%d", label_id);
    if (!v3_push_loop_labels(continue_label, end_label)) {
        return false;
    }
    v3_text_appendf(out, cap, "%s:\n", start_label);
    v3_emit_load_slot(out, cap, &locals[iter_local_index], 9);
    if (!v3_codegen_expr_scalar(plan,
                                lowering,
                                function,
                                aliases,
                                alias_count,
                                locals,
                                local_count,
                                end_expr,
                                "i32",
                                10,
                                call_arg_base,
                                string_temp_base,
                                string_temp_stride,
                                0,
                                out,
                                cap)) {
        fprintf(stderr,
                "[cheng_v3_seed] emit for end expr failed function=%s stmt=%s end=%s\n",
                function->symbol_text,
                statement,
                end_expr);
        return false;
    }
    v3_text_appendf(out, cap,
                    "  cmp x9, x10\n"
                    "  b.ge %s\n",
                    end_label);
    if (inline_stmt[0] != '\0') {
        if (v3_startswith(inline_stmt, "if ") ||
            v3_startswith(inline_stmt, "while ") ||
            v3_startswith(inline_stmt, "for ")) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit for inline compound reject function=%s inline=%s\n",
                    function->symbol_text,
                    inline_stmt);
            return false;
        }
        if (!v3_emit_non_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      inline_stmt,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      control_label_index_io,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap,
                                      &branch_terminated)) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit for inline stmt failed function=%s inline=%s\n",
                    function->symbol_text,
                    inline_stmt);
            v3_pop_loop_labels();
            return false;
        }
        v3_text_appendf(out, cap, "%s:\n", continue_label);
        v3_emit_load_slot(out, cap, &locals[iter_local_index], 9);
        v3_text_appendf(out, cap, "  add x9, x9, #1\n");
        v3_emit_store_slot(out, cap, &locals[iter_local_index], 9);
        v3_text_appendf(out, cap, "  b %s\n", start_label);
        v3_text_appendf(out, cap, "%s:\n", end_label);
        v3_pop_loop_labels();
        return true;
    }
    while (*index_io < line_count) {
        char raw_copy[4096];
        char *trimmed;
        int32_t indent;
        char nested_stmt[8192];
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[*index_io]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            *index_io += 1U;
            continue;
        }
        indent = v3_line_indent(lines[*index_io]);
        if (indent <= parent_indent) {
            break;
        }
        if (branch_terminated) {
            *index_io += 1U;
            continue;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, index_io, nested_stmt, sizeof(nested_stmt))) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit for collect nested stmt failed function=%s parent=%s\n",
                    function->symbol_text,
                    statement);
            v3_pop_loop_labels();
            return false;
        }
        if (v3_startswith(nested_stmt, "if ")) {
            if (!v3_emit_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      lines,
                                      line_count,
                                      index_io,
                                      indent,
                                      nested_stmt,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      control_label_index_io,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit for nested if failed function=%s parent=%s nested=%s\n",
                        function->symbol_text,
                        statement,
                        nested_stmt);
                v3_pop_loop_labels();
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "while ")) {
            if (!v3_emit_while_statement(plan,
                                         lowering,
                                         function,
                                         aliases,
                                         alias_count,
                                         locals,
                                         local_count,
                                         lines,
                                         line_count,
                                         index_io,
                                         indent,
                                         nested_stmt,
                                         call_arg_base,
                                         string_temp_base,
                                         string_temp_stride,
                                         string_label_index_io,
                                         control_label_index_io,
                                         frame_size,
                                         sret_local_index,
                                         out,
                                         cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit for nested while failed function=%s parent=%s nested=%s\n",
                        function->symbol_text,
                        statement,
                        nested_stmt);
                v3_pop_loop_labels();
                return false;
            }
            continue;
        }
        if (v3_startswith(nested_stmt, "for ")) {
            if (!v3_emit_for_statement(plan,
                                       lowering,
                                       function,
                                       aliases,
                                       alias_count,
                                       locals,
                                       local_count,
                                       lines,
                                       line_count,
                                       index_io,
                                       indent,
                                       nested_stmt,
                                       call_arg_base,
                                       string_temp_base,
                                       string_temp_stride,
                                       string_label_index_io,
                                       control_label_index_io,
                                       frame_size,
                                       sret_local_index,
                                       out,
                                       cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit for nested for failed function=%s parent=%s nested=%s\n",
                        function->symbol_text,
                        statement,
                        nested_stmt);
                v3_pop_loop_labels();
                return false;
            }
            continue;
        }
        if (!v3_emit_non_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      nested_stmt,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      string_label_index_io,
                                      control_label_index_io,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap,
                                      &branch_terminated)) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit for nested stmt failed function=%s parent=%s nested=%s\n",
                    function->symbol_text,
                    statement,
                    nested_stmt);
            v3_pop_loop_labels();
            return false;
        }
    }
    v3_text_appendf(out, cap, "%s:\n", continue_label);
    v3_emit_load_slot(out, cap, &locals[iter_local_index], 9);
    v3_text_appendf(out, cap, "  add x9, x9, #1\n");
    v3_emit_store_slot(out, cap, &locals[iter_local_index], 9);
    v3_text_appendf(out, cap, "  b %s\n", start_label);
    v3_text_appendf(out, cap, "%s:\n", end_label);
    v3_pop_loop_labels();
    return true;
}

static bool v3_try_emit_scalar_function(const V3SystemLinkPlanStub *plan,
                                        const V3LoweringPlanStub *lowering,
                                        const V3LoweredFunctionStub *function,
                                        char *out,
                                        size_t cap,
                                        size_t *instruction_words_out) {
    char **lines = NULL;
    char *owned_lines = NULL;
    size_t line_count = 0U;
    size_t fn_line_index = 0U;
    char param_names[CHENG_V3_CONST_MAX_BINDINGS][128];
    size_t param_count = 0U;
    char owned_path[PATH_MAX];
    V3ImportAlias aliases[CHENG_V3_MAX_IMPORT_ALIASES];
    size_t alias_count = 0U;
    V3AsmLocalSlot locals[CHENG_V3_MAX_ASM_LOCALS];
    size_t local_count = 0U;
    int32_t next_offset = 0;
    int32_t max_call_depth = 0;
    int32_t call_arg_base = 0;
    int32_t max_string_literal_temps = 0;
    int32_t string_temp_base = 0;
    int32_t string_temp_stride = 0;
    int32_t string_label_index = 0;
    int32_t control_label_index = 0;
    int32_t sret_local_index = -1;
    int32_t frame_size;
    size_t i;
    bool saw_return = false;
    bool composite_return = strcmp(function->return_abi_class, "composite") == 0;
    bool void_return = strcmp(function->return_abi_class, "void") == 0;
    bool debug_chain_node_rebuild =
        strcmp(function->symbol_text, "cheng/v3/project/chain_node::v3ChainNodeRebuildDerived") == 0;
    bool has_inline_signature_body = false;
    char inline_signature_body[8192];
    inline_signature_body[0] = '\0';
    if (!v3_load_function_source_lines(function,
                                       &lines,
                                       &owned_lines,
                                       &line_count,
                                       &fn_line_index,
                                       param_names,
                                       &param_count,
                                       owned_path)) {
        return false;
    }
    if (debug_chain_node_rebuild && !instruction_words_out) {
        fprintf(stderr, "[cheng_v3_seed] rebuildDerived enter emit pass\n");
    }
    v3_reset_loop_labels();
    v3_parse_import_aliases_from_lines(lines, line_count, aliases, &alias_count, CHENG_V3_MAX_IMPORT_ALIASES);
    {
        char signature_copy[8192];
        char *eq;
        snprintf(signature_copy, sizeof(signature_copy), "%s", lines[fn_line_index]);
        eq = strrchr(signature_copy, '=');
        if (eq) {
            v3_trim_copy_text(eq + 1, inline_signature_body, sizeof(inline_signature_body));
            has_inline_signature_body = inline_signature_body[0] != '\0';
        }
    }
    memset(locals, 0, sizeof(locals));
    for (i = 0; i < function->param_count; ++i) {
        if (!v3_add_local_slot_for_type(lowering,
                                        function->owner_module_path,
                                        aliases,
                                        alias_count,
                                        locals,
                                        &local_count,
                                        CHENG_V3_MAX_ASM_LOCALS,
                                        function->param_names[i],
                                        function->param_types[i],
                                        function->param_abi_classes[i],
                                        function->param_is_var[i] ||
                                        !v3_abi_class_scalar_or_ptr(function->param_abi_classes[i]),
                                        &next_offset)) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit param slot failed function=%s param=%s type=%s abi=%s\n",
                    function->symbol_text,
                    function->param_names[i],
                    function->param_types[i],
                    function->param_abi_classes[i]);
            free(lines);
            free(owned_lines);
            return false;
        }
    }
    if (composite_return) {
        sret_local_index = (int32_t)local_count;
        if (!v3_add_local_slot_sized(locals,
                                     &local_count,
                                     CHENG_V3_MAX_ASM_LOCALS,
                                     "__v3_sret",
                                     "ptr",
                                     "ptr",
                                     true,
                                     8,
                                     8,
                                     &next_offset)) {
            free(lines);
            free(owned_lines);
            return false;
        }
    }
    if (has_inline_signature_body &&
        strcmp(function->body_kind, "return_expr") == 0) {
        if (v3_call_expr_string_literal_count(inline_signature_body) > max_string_literal_temps) {
            max_string_literal_temps = v3_call_expr_string_literal_count(inline_signature_body);
        }
        if (!v3_prepare_expr_call_state(plan,
                                        lowering,
                                        function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        &local_count,
                                        CHENG_V3_MAX_ASM_LOCALS,
                                        &next_offset,
                                        inline_signature_body,
                                        0,
                                        &max_call_depth)) {
            fprintf(stderr,
                    "[cheng_v3_seed] prepare inline return call state failed function=%s expr=%s\n",
                    function->symbol_text,
                    inline_signature_body);
            free(lines);
            free(owned_lines);
            return false;
        }
    }
    for (i = fn_line_index + 1U; i < line_count; ) {
        char statement[8192];
        char name[128];
        char type_text[128];
        char expr[4096];
        char raw_copy[4096];
        char *trimmed;
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[i]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            i += 1U;
            continue;
        }
        if (v3_line_indent(lines[i]) <= 0 && v3_startswith(trimmed, "fn ")) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, &i, statement, sizeof(statement))) {
            free(lines);
            free(owned_lines);
            return false;
        }
        if (debug_chain_node_rebuild && !instruction_words_out) {
            fprintf(stderr, "[cheng_v3_seed] rebuildDerived prepare stmt=%s\n", statement);
        }
        if (v3_startswith(statement, "if ")) {
            if (!v3_prepare_if_statement_state(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               &local_count,
                                               CHENG_V3_MAX_ASM_LOCALS,
                                               &next_offset,
                                               lines,
                                               line_count,
                                               &i,
                                               v3_line_indent(lines[i - 1U]),
                                               statement,
                                               &max_call_depth,
                                               &max_string_literal_temps)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare if state failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_startswith(statement, "while ")) {
            if (!v3_prepare_while_statement_state(plan,
                                                  lowering,
                                                  function,
                                                  aliases,
                                                  alias_count,
                                                  locals,
                                                  &local_count,
                                                  CHENG_V3_MAX_ASM_LOCALS,
                                                  &next_offset,
                                                  lines,
                                                  line_count,
                                                  &i,
                                                  v3_line_indent(lines[i - 1U]),
                                                  statement,
                                                  &max_call_depth,
                                                  &max_string_literal_temps)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare while state failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_startswith(statement, "for ")) {
            if (!v3_prepare_for_statement_state(plan,
                                                lowering,
                                                function,
                                                aliases,
                                                alias_count,
                                                locals,
                                                &local_count,
                                                CHENG_V3_MAX_ASM_LOCALS,
                                                &next_offset,
                                                lines,
                                                line_count,
                                                &i,
                                                v3_line_indent(lines[i - 1U]),
                                                statement,
                                                &max_call_depth,
                                                &max_string_literal_temps)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare for state failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_startswith(statement, "case ")) {
            if (!v3_prepare_case_const_table_statement(plan,
                                                       lowering,
                                                       function,
                                                       aliases,
                                                       alias_count,
                                                       locals,
                                                       &local_count,
                                                       CHENG_V3_MAX_ASM_LOCALS,
                                                       &next_offset,
                                                       lines,
                                                       line_count,
                                                       &i,
                                                       v3_line_indent(lines[i - 1U]),
                                                       statement,
                                                       &max_call_depth,
                                                       &max_string_literal_temps)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare case state failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (strcmp(function->body_kind, "return_expr") == 0 &&
            strcmp(statement, function->first_body_line) == 0 &&
            v3_is_last_logical_statement_in_function(lines, line_count, i) &&
            !v3_startswith(statement, "return ")) {
            if (v3_call_expr_string_literal_count(statement) > max_string_literal_temps) {
                max_string_literal_temps = v3_call_expr_string_literal_count(statement);
            }
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            &local_count,
                                            CHENG_V3_MAX_ASM_LOCALS,
                                            &next_offset,
                                            statement,
                                            0,
                                            &max_call_depth)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare implicit return call state failed function=%s expr=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
            v3_parse_binding_decl_meta(statement,
                                       v3_startswith(statement, "let ") ? "let " : "var ",
                                       name,
                                       sizeof(name),
                                       type_text,
                                       sizeof(type_text))) {
            const char *abi_class = v3_type_abi_class(type_text);
            if (v3_find_local_slot(locals, local_count, name) < 0) {
                if (!v3_add_local_slot_for_type(lowering,
                                                function->owner_module_path,
                                                aliases,
                                                alias_count,
                                                locals,
                                                &local_count,
                                                CHENG_V3_MAX_ASM_LOCALS,
                                                name,
                                                type_text,
                                                abi_class,
                                                false,
                                                &next_offset)) {
                    free(lines);
                    free(owned_lines);
                    return false;
                }
            }
            continue;
        }
        if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
            v3_parse_binding_meta(statement,
                                  v3_startswith(statement, "let ") ? "let " : "var ",
                                  name,
                                  sizeof(name),
                                  type_text,
                                  sizeof(type_text),
                                  expr,
                                  sizeof(expr))) {
            const char *abi_class;
            char inferred_type[128];
            char inferred_abi[32];
            if (type_text[0] == '\0') {
                if (!v3_infer_expr_type(plan,
                                        lowering,
                                        function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        expr,
                                        inferred_type,
                                        sizeof(inferred_type),
                                        inferred_abi,
                                        sizeof(inferred_abi))) {
                    fprintf(stderr,
                            "[cheng_v3_seed] prepare binding infer type failed function=%s local=%s expr=%s\n",
                            function->symbol_text,
                            name,
                            expr);
                    free(lines);
                    free(owned_lines);
                    return false;
                }
                snprintf(type_text, sizeof(type_text), "%s", inferred_type);
            }
            abi_class = v3_type_abi_class(type_text);
            if (type_text[0] == '\0' || strcmp(abi_class, "void") == 0) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare binding invalid type function=%s local=%s type=%s expr=%s\n",
                        function->symbol_text,
                        name,
                        type_text,
                        expr);
                free(lines);
                free(owned_lines);
                return false;
            }
            if (v3_find_local_slot(locals, local_count, name) < 0) {
                if (!v3_add_local_slot_for_type(lowering,
                                                function->owner_module_path,
                                                aliases,
                                                alias_count,
                                                locals,
                                                &local_count,
                                                CHENG_V3_MAX_ASM_LOCALS,
                                                name,
                                                type_text,
                                                abi_class,
                                                false,
                                                &next_offset)) {
                    fprintf(stderr,
                            "[cheng_v3_seed] emit local slot failed function=%s local=%s type=%s abi=%s stmt=%s\n",
                            function->symbol_text,
                            name,
                            type_text,
                            abi_class,
                            statement);
                    free(lines);
                    free(owned_lines);
                    return false;
                }
            }
            if (v3_call_expr_string_literal_count(expr) > max_string_literal_temps) {
                max_string_literal_temps = v3_call_expr_string_literal_count(expr);
            }
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            &local_count,
                                            CHENG_V3_MAX_ASM_LOCALS,
                                            &next_offset,
                                            expr,
                                            0,
                                            &max_call_depth)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare expr call state failed function=%s expr=%s\n",
                        function->symbol_text,
                        expr);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (strcmp(statement, "return") == 0) {
            continue;
        }
        if (v3_startswith(statement, "return ")) {
            if (v3_call_expr_string_literal_count(statement + 7) > max_string_literal_temps) {
                max_string_literal_temps = v3_call_expr_string_literal_count(statement + 7);
            }
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            &local_count,
                                            CHENG_V3_MAX_ASM_LOCALS,
                                            &next_offset,
                                            statement + 7,
                                            0,
                                            &max_call_depth)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare return call state failed function=%s expr=%s\n",
                        function->symbol_text,
                        statement + 7);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_parse_assignment_meta(statement,
                                     name,
                                     sizeof(name),
                                     expr,
                                     sizeof(expr))) {
            if (v3_call_expr_string_literal_count(expr) > max_string_literal_temps) {
                max_string_literal_temps = v3_call_expr_string_literal_count(expr);
            }
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            &local_count,
                                            CHENG_V3_MAX_ASM_LOCALS,
                                            &next_offset,
                                            expr,
                                            0,
                                            &max_call_depth)) {
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (strchr(statement, '(') != NULL) {
            if (v3_call_expr_string_literal_count(statement) > max_string_literal_temps) {
                max_string_literal_temps = v3_call_expr_string_literal_count(statement);
            }
            if (!v3_prepare_expr_call_state(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            &local_count,
                                            CHENG_V3_MAX_ASM_LOCALS,
                                            &next_offset,
                                            statement,
                                            0,
                                            &max_call_depth)) {
                fprintf(stderr,
                        "[cheng_v3_seed] prepare stmt call state failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        free(lines);
        free(owned_lines);
        return false;
    }
    if (debug_chain_node_rebuild && !instruction_words_out) {
        fprintf(stderr,
                "[cheng_v3_seed] rebuildDerived prepare done locals=%zu next_offset=%d max_call_depth=%d\n",
                local_count,
                next_offset,
                max_call_depth);
    }
    call_arg_base = next_offset;
    next_offset += (max_call_depth > 0 ? max_call_depth : 1) * CHENG_V3_CALL_ARG_SPILL_STRIDE;
    string_temp_base = next_offset;
    string_temp_stride = (max_string_literal_temps > 0 ? max_string_literal_temps : 1) * 24;
    next_offset += string_temp_stride * (max_call_depth > 0 ? max_call_depth : 1);
    frame_size = ((next_offset + 15 + 16) / 16) * 16;
    if (frame_size < 16) {
        frame_size = 16;
    }
    out[0] = '\0';
    v3_text_appendf(out, cap,
                    ".globl ");
    {
        char symbol_name[PATH_MAX];
        v3_primary_symbol_name(plan, function, symbol_name, sizeof(symbol_name));
        int32_t save_offset = frame_size - 16;
        v3_text_appendf(out, cap,
                        "%s\n"
                        "%s:\n",
                        symbol_name,
                        symbol_name);
        if (!v3_emit_sp_adjust(out, cap, true, frame_size)) {
            free(lines);
            free(owned_lines);
            return false;
        }
        if (save_offset <= 504 && (save_offset % 8) == 0) {
            v3_text_appendf(out, cap, "  stp x29, x30, [sp, #%d]\n", save_offset);
        } else if (!v3_emit_sp_offset_address(out, cap, 16, save_offset)) {
            free(lines);
            free(owned_lines);
            return false;
        } else {
            v3_text_appendf(out, cap, "  stp x29, x30, [x16]\n");
        }
        if (!v3_emit_sp_offset_address(out, cap, 29, save_offset)) {
            free(lines);
            free(owned_lines);
            return false;
        }
    }
    for (i = 0; i < function->param_count; ++i) {
        if (i < CHENG_V3_CALL_ARG_REGS) {
            if (!v3_emit_sp_store_reg(out,
                                      cap,
                                      locals[i].stack_offset,
                                      function->param_abi_classes[i],
                                      (int)i)) {
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (!v3_emit_sp_load_reg(out,
                                 cap,
                                 frame_size + (int32_t)(i - CHENG_V3_CALL_ARG_REGS) * 8,
                                 function->param_abi_classes[i],
                                 9) ||
            !v3_emit_sp_store_reg(out,
                                  cap,
                                  locals[i].stack_offset,
                                  function->param_abi_classes[i],
                                  9)) {
            free(lines);
            free(owned_lines);
            return false;
        }
    }
    if (composite_return && sret_local_index >= 0) {
        if (!v3_emit_sp_store_reg(out,
                                  cap,
                                  locals[sret_local_index].stack_offset,
                                  "ptr",
                                  8)) {
            free(lines);
            free(owned_lines);
            return false;
        }
    }
    if (has_inline_signature_body &&
        strcmp(function->body_kind, "return_expr") == 0) {
        if (composite_return) {
            if (sret_local_index < 0 ||
                !v3_emit_slot_address(out, cap, &locals[sret_local_index], 15) ||
                !v3_materialize_composite_expr_into_address(plan,
                                                            lowering,
                                                            function,
                                                            aliases,
                                                            alias_count,
                                                            locals,
                                                            local_count,
                                                            inline_signature_body,
                                                            function->return_type,
                                                            15,
                                                            call_arg_base,
                                                            string_temp_base,
                                                            string_temp_stride,
                                                            &string_label_index,
                                                            0,
                                                            out,
                                                            cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit inline composite return failed function=%s expr=%s type=%s\n",
                        function->symbol_text,
                        inline_signature_body,
                        function->return_type);
                free(lines);
                free(owned_lines);
                return false;
            }
        } else if (!v3_codegen_expr_scalar(plan,
                                           lowering,
                                           function,
                                           aliases,
                                           alias_count,
                                           locals,
                                           local_count,
                                           inline_signature_body,
                                           function->return_abi_class,
                                           9,
                                           call_arg_base,
                                           string_temp_base,
                                           string_temp_stride,
                                           0,
                                           out,
                                           cap)) {
            fprintf(stderr,
                    "[cheng_v3_seed] emit inline return failed function=%s expr=%s abi=%s\n",
                    function->symbol_text,
                    inline_signature_body,
                    function->return_abi_class);
            free(lines);
            free(owned_lines);
            return false;
        }
        if (!v3_emit_function_epilogue(out, cap, frame_size)) {
            free(lines);
            free(owned_lines);
            return false;
        }
        saw_return = true;
    }
    for (i = fn_line_index + 1U; !saw_return && i < line_count; ) {
        char statement[8192];
        char name[128];
        char type_text[128];
        char expr[4096];
        char raw_copy[4096];
        char *trimmed;
        snprintf(raw_copy, sizeof(raw_copy), "%s", lines[i]);
        trimmed = v3_trim_inplace(raw_copy);
        if (*trimmed == '\0') {
            i += 1U;
            continue;
        }
        if (v3_line_indent(lines[i]) <= 0 && v3_startswith(trimmed, "fn ")) {
            break;
        }
        if (!v3_collect_statement_from_lines(lines, line_count, &i, statement, sizeof(statement))) {
            free(lines);
            free(owned_lines);
            return false;
        }
        if (debug_chain_node_rebuild && !instruction_words_out) {
            fprintf(stderr, "[cheng_v3_seed] rebuildDerived emit stmt=%s\n", statement);
        }
        if (v3_startswith(statement, "if ")) {
            if (!v3_emit_if_statement(plan,
                                      lowering,
                                      function,
                                      aliases,
                                      alias_count,
                                      locals,
                                      local_count,
                                      lines,
                                      line_count,
                                      &i,
                                      v3_line_indent(lines[i - 1U]),
                                      statement,
                                      call_arg_base,
                                      string_temp_base,
                                      string_temp_stride,
                                      &string_label_index,
                                      &control_label_index,
                                      frame_size,
                                      sret_local_index,
                                      out,
                                      cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit if statement failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_startswith(statement, "while ")) {
            if (!v3_emit_while_statement(plan,
                                         lowering,
                                         function,
                                         aliases,
                                         alias_count,
                                         locals,
                                         local_count,
                                         lines,
                                         line_count,
                                         &i,
                                         v3_line_indent(lines[i - 1U]),
                                         statement,
                                         call_arg_base,
                                         string_temp_base,
                                         string_temp_stride,
                                         &string_label_index,
                                         &control_label_index,
                                         frame_size,
                                         sret_local_index,
                                         out,
                                         cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit while statement failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_startswith(statement, "for ")) {
            if (!v3_emit_for_statement(plan,
                                       lowering,
                                       function,
                                       aliases,
                                       alias_count,
                                       locals,
                                       local_count,
                                       lines,
                                       line_count,
                                       &i,
                                       v3_line_indent(lines[i - 1U]),
                                       statement,
                                       call_arg_base,
                                       string_temp_base,
                                       string_temp_stride,
                                       &string_label_index,
                                       &control_label_index,
                                       frame_size,
                                       sret_local_index,
                                       out,
                                       cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit for statement failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_startswith(statement, "case ")) {
            bool case_terminated = false;
            if (!v3_emit_case_const_table_statement(plan,
                                                    lowering,
                                                    function,
                                                    aliases,
                                                    alias_count,
                                                    locals,
                                                    local_count,
                                                    lines,
                                                    line_count,
                                                    &i,
                                                    v3_line_indent(lines[i - 1U]),
                                                    statement,
                                                    call_arg_base,
                                                    string_temp_base,
                                                    string_temp_stride,
                                                    &string_label_index,
                                                    &control_label_index,
                                                    frame_size,
                                                    sret_local_index,
                                                    &case_terminated,
                                                    out,
                                                    cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit case statement failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            if (case_terminated) {
                saw_return = true;
                break;
            }
            continue;
        }
        if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
            v3_parse_binding_decl_meta(statement,
                                       v3_startswith(statement, "let ") ? "let " : "var ",
                                       name,
                                       sizeof(name),
                                       type_text,
                                       sizeof(type_text))) {
            int32_t local_index = v3_find_local_slot(locals, local_count, name);
            if (local_index < 0 || !v3_emit_zero_slot(out, cap, &locals[local_index])) {
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if ((v3_startswith(statement, "let ") || v3_startswith(statement, "var ")) &&
            v3_parse_binding_meta(statement,
                                  v3_startswith(statement, "let ") ? "let " : "var ",
                                  name,
                                  sizeof(name),
                                  type_text,
                                  sizeof(type_text),
                                  expr,
                                  sizeof(expr))) {
            int32_t local_index = v3_find_local_slot(locals, local_count, name);
            const char *abi_class;
            char inferred_type[128];
            char inferred_abi[32];
            if (type_text[0] == '\0') {
                if (!v3_infer_expr_type(plan,
                                        lowering,
                                        function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        expr,
                                        inferred_type,
                                        sizeof(inferred_type),
                                        inferred_abi,
                                        sizeof(inferred_abi))) {
                    fprintf(stderr,
                            "[cheng_v3_seed] emit binding infer type failed function=%s local=%s expr=%s\n",
                            function->symbol_text,
                            name,
                            expr);
                    free(lines);
                    free(owned_lines);
                    return false;
                }
                snprintf(type_text, sizeof(type_text), "%s", inferred_type);
            }
            abi_class = v3_type_abi_class(type_text);
            if (local_index < 0 || locals[local_index].stack_offset < 0) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit local missing function=%s local=%s stmt=%s\n",
                        function->symbol_text,
                        name,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            if (v3_abi_class_scalar_or_ptr(abi_class)) {
                if (!v3_codegen_expr_scalar(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            expr,
                                            abi_class,
                                            9,
                                            call_arg_base,
                                            string_temp_base,
                                            string_temp_stride,
                                            0,
                                            out,
                                            cap)) {
                    fprintf(stderr,
                            "[cheng_v3_seed] emit scalar binding failed function=%s local=%s abi=%s expr=%s\n",
                            function->symbol_text,
                            name,
                            abi_class,
                            expr);
                    free(lines);
                    free(owned_lines);
                    return false;
                }
                v3_emit_store_slot(out, cap, &locals[local_index], 9);
                continue;
            }
            if (!v3_materialize_composite_expr_into_slot(plan,
                                                         lowering,
                                                         function,
                                                         aliases,
                                                         alias_count,
                                                         locals,
                                                         local_count,
                                                         name,
                                                         0U,
                                                         expr,
                                                         &locals[local_index],
                                                         call_arg_base,
                                                         string_temp_base,
                                                         string_temp_stride,
                                                         &string_label_index,
                                                         0,
                                                         out,
                                                         cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit composite binding failed function=%s local=%s type=%s expr=%s\n",
                        function->symbol_text,
                        name,
                        type_text,
                        expr);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        if (v3_parse_assignment_meta(statement,
                                     name,
                                     sizeof(name),
                                     expr,
                                     sizeof(expr))) {
            int32_t local_index = v3_find_local_slot(locals, local_count, name);
            char field_type[256];
            char field_abi[32];
            if (local_index >= 0) {
                const char *abi_class = locals[local_index].abi_class;
                if (v3_abi_class_scalar_or_ptr(abi_class)) {
                    if (!v3_codegen_expr_scalar(plan,
                                                lowering,
                                                function,
                                                aliases,
                                                alias_count,
                                                locals,
                                                local_count,
                                                expr,
                                                abi_class,
                                                9,
                                                call_arg_base,
                                                string_temp_base,
                                                string_temp_stride,
                                                0,
                                                out,
                                                cap)) {
                        free(lines);
                        free(owned_lines);
                        return false;
                    }
                    v3_emit_store_slot(out, cap, &locals[local_index], 9);
                    continue;
                }
                if (!v3_materialize_composite_expr_into_slot(plan,
                                                             lowering,
                                                             function,
                                                             aliases,
                                                             alias_count,
                                                             locals,
                                                             local_count,
                                                             name,
                                                             0U,
                                                             expr,
                                                             &locals[local_index],
                                                             call_arg_base,
                                                             string_temp_base,
                                                             string_temp_stride,
                                                             &string_label_index,
                                                             0,
                                                             out,
                                                             cap)) {
                    free(lines);
                    free(owned_lines);
                    return false;
                }
                continue;
            }
            if (v3_infer_lvalue_expr_type(plan,
                                          lowering,
                                          function,
                                          aliases,
                                          alias_count,
                                          locals,
                                          local_count,
                                          name,
                                          field_type,
                                          sizeof(field_type),
                                          field_abi,
                                          sizeof(field_abi))) {
                if (v3_abi_class_scalar_or_ptr(field_abi)) {
                    if (!v3_codegen_expr_scalar(plan,
                                                lowering,
                                                function,
                                                aliases,
                                                alias_count,
                                                locals,
                                                local_count,
                                                expr,
                                                field_abi,
                                                9,
                                                call_arg_base,
                                                string_temp_base,
                                                string_temp_stride,
                                                0,
                                                out,
                                                cap)) {
                        free(lines);
                        free(owned_lines);
                        return false;
                    }
                    if (!v3_emit_lvalue_address(plan,
                                                lowering,
                                                function,
                                                aliases,
                                                alias_count,
                                                locals,
                                                local_count,
                                                name,
                                                field_type,
                                                sizeof(field_type),
                                                field_abi,
                                                sizeof(field_abi),
                                                15,
                                                call_arg_base,
                                                string_temp_base,
                                                string_temp_stride,
                                                0,
                                                out,
                                                cap)) {
                        free(lines);
                        free(owned_lines);
                        return false;
                    }
                    v3_emit_store_scalar_to_address(out, cap, 15, 0, field_abi, 9);
                    continue;
                }
                if (!v3_emit_lvalue_address(plan,
                                            lowering,
                                            function,
                                            aliases,
                                            alias_count,
                                            locals,
                                            local_count,
                                            name,
                                            field_type,
                                            sizeof(field_type),
                                            field_abi,
                                            sizeof(field_abi),
                                            15,
                                            call_arg_base,
                                            string_temp_base,
                                            string_temp_stride,
                                            0,
                                            out,
                                            cap)) {
                    free(lines);
                    free(owned_lines);
                    return false;
                }
                if (!v3_materialize_composite_expr_into_address(plan,
                                                                lowering,
                                                                function,
                                                                aliases,
                                                                alias_count,
                                                                locals,
                                                                local_count,
                                                                expr,
                                                                field_type,
                                                                15,
                                                                call_arg_base,
                                                                string_temp_base,
                                                                string_temp_stride,
                                                                &string_label_index,
                                                                0,
                                                                out,
                                                                cap)) {
                    free(lines);
                    free(owned_lines);
                    return false;
                }
                continue;
            }
            free(lines);
            free(owned_lines);
            return false;
        }
        if (strcmp(statement, "return") == 0) {
            if (!void_return) {
                free(lines);
                free(owned_lines);
                return false;
            }
            if (!v3_emit_function_epilogue(out, cap, frame_size)) {
                free(lines);
                free(owned_lines);
                return false;
            }
            saw_return = true;
            break;
        }
        if (strcmp(function->body_kind, "return_expr") == 0 &&
            strcmp(statement, function->first_body_line) == 0 &&
            v3_is_last_logical_statement_in_function(lines, line_count, i) &&
            !v3_startswith(statement, "return ")) {
            if (composite_return) {
                if (sret_local_index < 0 ||
                    !v3_emit_slot_address(out, cap, &locals[sret_local_index], 15) ||
                    !v3_materialize_composite_expr_into_address(plan,
                                                                lowering,
                                                                function,
                                                                aliases,
                                                                alias_count,
                                                                locals,
                                                                local_count,
                                                                statement,
                                                                function->return_type,
                                                                15,
                                                                call_arg_base,
                                                                string_temp_base,
                                                                string_temp_stride,
                                                                &string_label_index,
                                                                0,
                                                                out,
                                                                cap)) {
                    fprintf(stderr,
                            "[cheng_v3_seed] emit implicit composite return failed function=%s expr=%s type=%s\n",
                            function->symbol_text,
                            statement,
                            function->return_type);
                    free(lines);
                    free(owned_lines);
                    return false;
                }
            } else if (!v3_codegen_expr_scalar(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               statement,
                                               function->return_abi_class,
                                               9,
                                               call_arg_base,
                                               string_temp_base,
                                               string_temp_stride,
                                               0,
                                               out,
                                               cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit implicit return failed function=%s expr=%s abi=%s\n",
                        function->symbol_text,
                        statement,
                        function->return_abi_class);
                free(lines);
                free(owned_lines);
                return false;
            } else {
                v3_emit_reg_to_return(out, cap, 9, function->return_abi_class);
            }
            if (!v3_emit_function_epilogue(out, cap, frame_size)) {
                free(lines);
                free(owned_lines);
                return false;
            }
            saw_return = true;
            break;
        }
        if (v3_startswith(statement, "return ")) {
            if (composite_return) {
                if (sret_local_index < 0 ||
                    !v3_emit_slot_address(out, cap, &locals[sret_local_index], 15) ||
                    !v3_materialize_composite_expr_into_address(plan,
                                                                lowering,
                                                                function,
                                                                aliases,
                                                                alias_count,
                                                                locals,
                                                                local_count,
                                                                statement + 7,
                                                                function->return_type,
                                                                15,
                                                                call_arg_base,
                                                                string_temp_base,
                                                                string_temp_stride,
                                                                &string_label_index,
                                                                0,
                                                                out,
                                                                cap)) {
                    fprintf(stderr,
                            "[cheng_v3_seed] emit composite return failed function=%s expr=%s type=%s\n",
                            function->symbol_text,
                            statement + 7,
                            function->return_type);
                    free(lines);
                    free(owned_lines);
                    return false;
                }
            } else if (!v3_codegen_expr_scalar(plan,
                                               lowering,
                                               function,
                                               aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               statement + 7,
                                               function->return_abi_class,
                                               9,
                                               call_arg_base,
                                               string_temp_base,
                                               string_temp_stride,
                                               0,
                                               out,
                                               cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit return failed function=%s expr=%s abi=%s\n",
                        function->symbol_text,
                        statement + 7,
                        function->return_abi_class);
                free(lines);
                free(owned_lines);
                return false;
            } else {
                v3_emit_reg_to_return(out, cap, 9, function->return_abi_class);
            }
            if (!v3_emit_function_epilogue(out, cap, frame_size)) {
                free(lines);
                free(owned_lines);
                return false;
            }
            saw_return = true;
            break;
        }
    if (strchr(statement, '(') != NULL) {
        bool handled = false;
        char callee[PATH_MAX];
        char args[CHENG_V3_MAX_CALL_ARGS][4096];
        size_t arg_count = 0U;
        callee[0] = '\0';
        v3_parse_call_text(statement, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS);
        if (!v3_try_emit_builtin_statement(plan,
                                           lowering,
                                           function,
                                           aliases,
                                               alias_count,
                                               locals,
                                               local_count,
                                               statement,
                                               call_arg_base,
                                               string_temp_base,
                                               string_temp_stride,
                                               &string_label_index,
                                               &control_label_index,
                                               out,
                                               cap,
                                               &handled)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit builtin statement failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            if (handled) {
                if (strcmp(callee, "panic") == 0 || strcmp(callee, "system.panic") == 0) {
                    saw_return = true;
                    break;
                }
                continue;
            }
            if (!v3_parse_call_text(statement, callee, sizeof(callee), args, &arg_count, CHENG_V3_MAX_CALL_ARGS) ||
                !v3_codegen_call_scalar(plan,
                                        lowering,
                                        function,
                                        aliases,
                                        alias_count,
                                        locals,
                                        local_count,
                                        callee,
                                        args,
                                        arg_count,
                                        "void",
                                        9,
                                        call_arg_base,
                                        string_temp_base,
                                        string_temp_stride,
                                        &string_label_index,
                                        0,
                                        out,
                                        cap)) {
                fprintf(stderr,
                        "[cheng_v3_seed] emit stmt call failed function=%s stmt=%s\n",
                        function->symbol_text,
                        statement);
                free(lines);
                free(owned_lines);
                return false;
            }
            continue;
        }
        free(lines);
        free(owned_lines);
        fprintf(stderr,
                "[cheng_v3_seed] emit top-level stmt unsupported function=%s stmt=%s\n",
                function->symbol_text,
                statement);
        return false;
    }
    if (!saw_return) {
        if (!void_return) {
            free(lines);
            free(owned_lines);
            fprintf(stderr,
                    "[cheng_v3_seed] emit function fell through without terminal return function=%s return_type=%s body_kind=%s\n",
                    function->symbol_text,
                    function->return_type,
                    function->body_kind);
            return false;
        }
        if (!v3_emit_function_epilogue(out, cap, frame_size)) {
            free(lines);
            free(owned_lines);
            return false;
        }
    }
    free(lines);
    free(owned_lines);
    if (instruction_words_out) {
        *instruction_words_out = 16U;
    }
    return true;
}

static void v3_provider_source_for_module(const V3SystemLinkPlanStub *plan,
                                          const char *module_path,
                                          char *out,
                                          size_t cap) {
    if (v3_streq(module_path, "runtime/core_runtime_v3")) {
        v3_join_path(out, cap, plan->workspace_root, "v3/runtime/native/v3_core_runtime_stub.c");
        return;
    }
    if (v3_streq(module_path, "runtime/compiler_runtime_v3")) {
        if (v3_streq(plan->module_kind, "compiler_control_plane")) {
            v3_join_path(out, cap, plan->workspace_root, "v3/runtime/native/v3_tooling_argv_native.c");
        } else {
            v3_join_path(out, cap, plan->workspace_root, "v3/runtime/native/v3_program_argv_native.c");
        }
        return;
    }
    if (v3_streq(module_path, "runtime/program_support_v3")) {
        v3_join_path(out, cap, plan->workspace_root, "src/runtime/native/system_helpers.c");
        return;
    }
    out[0] = '\0';
}

static bool v3_build_primary_object_plan_stub(const V3SystemLinkPlanStub *plan,
                                              const V3LoweringPlanStub *lowering,
                                              V3PrimaryObjectPlanStub *primary) {
    size_t i;
    const V3LoweredFunctionStub *entry_function;
    V3ConstEntryProgram consteval_entry;
    memset(primary, 0, sizeof(*primary));
    memset(&consteval_entry, 0, sizeof(consteval_entry));
    snprintf(primary->object_format,
             sizeof(primary->object_format),
             "%s",
             v3_streq(plan->target_triple, "arm64-apple-darwin") ? "macho" : "unknown");
    snprintf(primary->output_path,
             sizeof(primary->output_path),
             "%s.primary.o",
             plan->output_path);
    snprintf(primary->entry_path,
             sizeof(primary->entry_path),
             "%s",
             plan->entry_path);
    snprintf(primary->entry_symbol,
             sizeof(primary->entry_symbol),
             "%s",
             plan->entry_symbol);
    entry_function = v3_find_entry_lowered_function(lowering);
    if (entry_function &&
        v3_streq(plan->emit_kind, "exe") &&
        v3_try_consteval_entry_program(plan, lowering, &consteval_entry)) {
        primary->consteval_entry_ready = true;
        primary->consteval_echo_count = consteval_entry.echo_count;
        primary->consteval_return_code = consteval_entry.return_code;
        for (i = 0; i < consteval_entry.echo_count; ++i) {
            snprintf(primary->consteval_echo_texts[i].text,
                     sizeof(primary->consteval_echo_texts[i].text),
                     "%s",
                     consteval_entry.echo_texts[i].text);
        }
        if (primary->item_count < CHENG_V3_MAX_PLAN_FUNCTIONS) {
            primary->item_ids[primary->item_count] = (int32_t)(100000 + (int32_t)primary->item_count);
            v3_primary_symbol_name(plan,
                                   entry_function,
                                   primary->symbol_names[primary->item_count].text,
                                   sizeof(primary->symbol_names[primary->item_count].text));
            primary->instruction_word_count += (consteval_entry.echo_count * 3U) + 2U;
            primary->data_label_count += consteval_entry.echo_count;
            primary->reloc_count += consteval_entry.echo_count * 2U;
            primary->item_count += 1U;
        } else {
            v3_primary_add_reason(primary, "primary_object_items_missing");
        }
        if (primary->item_count < CHENG_V3_MAX_PLAN_FUNCTIONS) {
            primary->item_ids[primary->item_count] = (int32_t)(900000 + (int32_t)primary->item_count);
            v3_runtime_entry_bridge_symbol(plan,
                                           primary->symbol_names[primary->item_count].text,
                                           sizeof(primary->symbol_names[primary->item_count].text));
            primary->instruction_word_count += 1U;
            primary->item_count += 1U;
        } else {
            v3_primary_add_reason(primary, "primary_object_entry_bridge_slot_missing");
        }
        if (plan->missing_reason_count > 0U) {
            v3_primary_add_reason(primary, "system_link_plan_not_ready_for_primary_object");
        }
        if (lowering->missing_reason_count > 0U) {
            v3_primary_add_reason(primary, "lowering_plan_not_ready_for_primary_object");
        }
        if (v3_streq(plan->emit_kind, "exe") && primary->entry_symbol[0] == '\0') {
            v3_primary_add_reason(primary, "primary_object_entry_symbol_missing");
        }
        return true;
    }
    for (i = 0; i < lowering->function_count && i < CHENG_V3_MAX_PLAN_FUNCTIONS; ++i) {
        primary->item_ids[primary->item_count] = (int32_t)(100000 + (int32_t)i);
        v3_primary_symbol_name(plan,
                               &lowering->functions[i],
                               primary->symbol_names[primary->item_count].text,
                               sizeof(primary->symbol_names[primary->item_count].text));
        if (strcmp(lowering->functions[i].body_kind, "return_zero_i32") == 0) {
            primary->instruction_word_count += 2U;
        } else if (strcmp(lowering->functions[i].body_kind, "return_call_noarg_i32") == 0) {
            const V3LoweredFunctionStub *callee =
                v3_find_lowered_function_for_tail_call(lowering,
                                                       &lowering->functions[i]);
            if (callee) {
                primary->instruction_word_count += 1U;
            } else {
                v3_primary_add_reason(primary, "primary_object_call_target_missing");
            }
        } else {
            size_t instruction_words = 0U;
            char *scratch = (char *)v3_xmalloc(1024U * 1024U);
            scratch[0] = '\0';
            if (v3_try_emit_scalar_function(plan,
                                            lowering,
                                            &lowering->functions[i],
                                            scratch,
                                            1024U * 1024U,
                                            &instruction_words)) {
                primary->instruction_word_count += instruction_words;
            } else {
                v3_primary_add_unsupported(primary, &lowering->functions[i]);
                primary->missing_reason_count += 0U;
            }
            free(scratch);
        }
        primary->item_count += 1U;
    }
    if (entry_function && v3_streq(plan->emit_kind, "exe")) {
        if (primary->item_count < CHENG_V3_MAX_PLAN_FUNCTIONS) {
            primary->item_ids[primary->item_count] = (int32_t)(900000 + (int32_t)primary->item_count);
            v3_runtime_entry_bridge_symbol(plan,
                                           primary->symbol_names[primary->item_count].text,
                                           sizeof(primary->symbol_names[primary->item_count].text));
            primary->instruction_word_count += 1U;
            primary->item_count += 1U;
        } else {
            v3_primary_add_reason(primary, "primary_object_entry_bridge_slot_missing");
        }
    }
    if (plan->missing_reason_count > 0U) {
        v3_primary_add_reason(primary, "system_link_plan_not_ready_for_primary_object");
    }
    if (lowering->missing_reason_count > 0U) {
        v3_primary_add_reason(primary, "lowering_plan_not_ready_for_primary_object");
    }
    if (primary->item_count <= 0U) {
        v3_primary_add_reason(primary, "primary_object_items_missing");
    }
    for (i = 0; i < lowering->function_count && i < CHENG_V3_MAX_PLAN_FUNCTIONS; ++i) {
        if (strcmp(lowering->functions[i].body_kind, "return_zero_i32") != 0 &&
            strcmp(lowering->functions[i].body_kind, "return_call_noarg_i32") != 0) {
            char *scratch = (char *)v3_xmalloc(1024U * 1024U);
            bool ok;
            scratch[0] = '\0';
            ok = v3_try_emit_scalar_function(plan,
                                             lowering,
                                             &lowering->functions[i],
                                             scratch,
                                             1024U * 1024U,
                                             NULL);
            free(scratch);
            if (!ok) {
                v3_primary_add_unsupported(primary, &lowering->functions[i]);
                v3_primary_add_reason(primary, "primary_object_body_semantics_missing");
                break;
            }
        }
    }
    if (primary->instruction_word_count <= 0U) {
        v3_primary_add_reason(primary, "primary_object_machine_words_missing");
    }
    if (v3_streq(plan->emit_kind, "exe") && primary->entry_symbol[0] == '\0') {
        v3_primary_add_reason(primary, "primary_object_entry_symbol_missing");
    }
    return true;
}

static bool v3_build_object_plan_stub(const V3SystemLinkPlanStub *plan,
                                      const V3LoweringPlanStub *lowering,
                                      const V3PrimaryObjectPlanStub *primary,
                                      V3ObjectPlanStub *object_plan) {
    size_t i;
    memset(object_plan, 0, sizeof(*object_plan));
    snprintf(object_plan->primary_object_path,
             sizeof(object_plan->primary_object_path),
             "%s",
             primary->output_path);
    if (object_plan->primary_object_path[0] != '\0') {
        v3_plan_add_path(object_plan->link_input_paths,
                         &object_plan->link_input_count,
                         CHENG_V3_MAX_PLAN_PATHS,
                         object_plan->primary_object_path);
    }
    for (i = 0; i < plan->provider_module_count; ++i) {
        char safe[PATH_MAX];
        char provider_source_path[PATH_MAX];
        char provider_object_path[PATH_MAX];
        v3_provider_source_for_module(plan,
                                      plan->provider_modules[i].text,
                                      provider_source_path,
                                      sizeof(provider_source_path));
        if (provider_source_path[0] != '\0') {
            v3_plan_add_path(object_plan->provider_source_paths,
                             &object_plan->provider_source_count,
                             CHENG_V3_MAX_PROVIDER_MODULES,
                             provider_source_path);
        }
        v3_sanitize_path_part(plan->provider_modules[i].text, safe, sizeof(safe));
        snprintf(provider_object_path,
                 sizeof(provider_object_path),
                 "%s.provider.%s.o",
                 plan->output_path,
                 safe);
        v3_plan_add_path(object_plan->provider_object_paths,
                         &object_plan->provider_object_count,
                         CHENG_V3_MAX_PROVIDER_MODULES,
                         provider_object_path);
        v3_plan_add_path(object_plan->link_input_paths,
                         &object_plan->link_input_count,
                         CHENG_V3_MAX_PLAN_PATHS,
                         provider_object_path);
    }
    if (plan->missing_reason_count > 0U) {
        v3_object_add_reason(object_plan, "system_link_plan_not_ready_for_object_plan");
    }
    if (lowering->missing_reason_count > 0U) {
        v3_object_add_reason(object_plan, "lowering_plan_not_ready_for_object_plan");
    }
    if (primary->missing_reason_count > 0U) {
        v3_object_add_reason(object_plan, "primary_object_not_ready_for_object_plan");
    }
    if (object_plan->primary_object_path[0] == '\0') {
        v3_object_add_reason(object_plan, "primary_object_path_missing");
    }
    if (lowering->function_count <= 0U) {
        v3_object_add_reason(object_plan, "primary_object_functions_missing");
    }
    if (plan->provider_module_count > 0U &&
        object_plan->provider_object_count != plan->provider_module_count) {
        v3_object_add_reason(object_plan, "provider_object_paths_incomplete");
    }
    if (plan->provider_module_count > 0U &&
        object_plan->provider_source_count != plan->provider_module_count) {
        v3_object_add_reason(object_plan, "provider_source_paths_incomplete");
    }
    return true;
}

static bool v3_build_native_link_plan_stub(const V3SystemLinkPlanStub *plan,
                                           const V3ObjectPlanStub *object_plan,
                                           V3NativeLinkPlanStub *native_link) {
    size_t i;
    memset(native_link, 0, sizeof(*native_link));
    snprintf(native_link->linker_program,
             sizeof(native_link->linker_program),
             "%s",
             "cc");
    snprintf(native_link->linker_flavor,
             sizeof(native_link->linker_flavor),
             "%s",
             v3_streq(plan->target_triple, "arm64-apple-darwin") ? "ld64" : "system_linker");
    snprintf(native_link->output_kind,
             sizeof(native_link->output_kind),
             "%s",
             v3_streq(plan->emit_kind, "exe") ? "executable" : "artifact");
    snprintf(native_link->primary_object_path,
             sizeof(native_link->primary_object_path),
             "%s",
             object_plan->primary_object_path);
    snprintf(native_link->entry_symbol,
             sizeof(native_link->entry_symbol),
             "%s",
             plan->entry_symbol[0] != '\0' ? plan->entry_symbol : "");
    for (i = 0; i < object_plan->link_input_count; ++i) {
        v3_plan_add_path(native_link->link_input_paths,
                         &native_link->link_input_count,
                         CHENG_V3_MAX_PLAN_PATHS,
                         object_plan->link_input_paths[i].text);
    }
    if (plan->missing_reason_count > 0U) {
        v3_native_link_add_reason(native_link, "system_link_plan_not_ready_for_native_link");
    }
    if (object_plan->missing_reason_count > 0U) {
        v3_native_link_add_reason(native_link, "object_plan_not_ready_for_native_link");
    }
    if (native_link->link_input_count <= 0U) {
        v3_native_link_add_reason(native_link, "native_link_inputs_missing");
    }
    if (plan->output_path[0] == '\0') {
        v3_native_link_add_reason(native_link, "native_link_output_missing");
    }
    if (v3_streq(plan->emit_kind, "exe") && native_link->entry_symbol[0] == '\0') {
        v3_native_link_add_reason(native_link, "native_link_entry_symbol_missing");
    }
    return true;
}

static bool v3_compile_c_object(const char *source_path, const char *out_path) {
    const char *cc = getenv("CC");
    char command[PATH_MAX * 4];
    char *quoted_cc;
    char *quoted_source;
    char *quoted_out;
    int rc;
    if (!cc || *cc == '\0') {
        cc = "cc";
    }
    if (!v3_ensure_parent_dir(out_path)) {
        return false;
    }
    quoted_cc = v3_shell_quote(cc);
    quoted_source = v3_shell_quote(source_path);
    quoted_out = v3_shell_quote(out_path);
    snprintf(command, sizeof(command),
             "%s -std=c11 -O2 -Wall -Wextra -pedantic -c %s -o %s",
             quoted_cc,
             quoted_source,
             quoted_out);
    free(quoted_cc);
    free(quoted_source);
    free(quoted_out);
    rc = system(command);
    if (rc != 0) {
        fprintf(stderr, "[cheng_v3_seed] provider object compile failed rc=%d command=%s\n", rc, command);
        return false;
    }
    return true;
}

static bool v3_compile_asm_object(const char *source_path, const char *out_path) {
    const char *cc = getenv("CC");
    char command[PATH_MAX * 4];
    char *quoted_cc;
    char *quoted_source;
    char *quoted_out;
    int rc;
    if (!cc || *cc == '\0') {
        cc = "cc";
    }
    if (!v3_ensure_parent_dir(out_path)) {
        return false;
    }
    quoted_cc = v3_shell_quote(cc);
    quoted_source = v3_shell_quote(source_path);
    quoted_out = v3_shell_quote(out_path);
    snprintf(command, sizeof(command),
             "%s -arch arm64 -c %s -o %s",
             quoted_cc,
             quoted_source,
             quoted_out);
    free(quoted_cc);
    free(quoted_source);
    free(quoted_out);
    rc = system(command);
    if (rc != 0) {
        fprintf(stderr, "[cheng_v3_seed] primary object compile failed rc=%d command=%s\n", rc, command);
        return false;
    }
    return true;
}

static bool v3_materialize_primary_object(const V3SystemLinkPlanStub *plan,
                                          const V3LoweringPlanStub *lowering,
                                          const V3PrimaryObjectPlanStub *primary) {
    size_t i;
    char asm_path[PATH_MAX];
    size_t cap = 65536U + lowering->function_count * 32768U;
    char *text;
    if (primary->missing_reason_count > 0U) {
        return false;
    }
    snprintf(asm_path, sizeof(asm_path), "%s.s", primary->output_path);
    if (primary->consteval_entry_ready) {
        const V3LoweredFunctionStub *entry_function = v3_find_entry_lowered_function(lowering);
        char entry_symbol[PATH_MAX];
        char bridge_symbol[PATH_MAX];
        if (!entry_function) {
            return false;
        }
        cap = 4096U + primary->consteval_echo_count * (PATH_MAX * 2U);
        text = (char *)v3_xmalloc(cap);
        text[0] = '\0';
        strncat(text, ".section __TEXT,__cstring,cstring_literals\n", cap - strlen(text) - 1U);
        for (i = 0; i < primary->consteval_echo_count; ++i) {
            char label[64];
            char *escaped = v3_c_escape(primary->consteval_echo_texts[i].text);
            char chunk[PATH_MAX * 2];
            snprintf(label, sizeof(label), "L_v3_const_echo_%zu", i);
            snprintf(chunk, sizeof(chunk),
                     "%s:\n"
                     "  .asciz \"%s\"\n",
                     label,
                     escaped);
            free(escaped);
            strncat(text, chunk, cap - strlen(text) - 1U);
        }
        strncat(text, ".text\n.p2align 2\n", cap - strlen(text) - 1U);
        v3_primary_symbol_name(plan, entry_function, entry_symbol, sizeof(entry_symbol));
        snprintf(text + strlen(text), cap - strlen(text),
                 ".globl %s\n"
                 "%s:\n",
                 entry_symbol,
                 entry_symbol);
        strncat(text,
                "  sub sp, sp, #16\n"
                "  str x30, [sp, #8]\n",
                cap - strlen(text) - 1U);
        for (i = 0; i < primary->consteval_echo_count; ++i) {
            char label[64];
            char chunk[256];
            snprintf(label, sizeof(label), "L_v3_const_echo_%zu", i);
            snprintf(chunk, sizeof(chunk),
                     "  adrp x0, %s@PAGE\n"
                     "  add x0, x0, %s@PAGEOFF\n"
                     "  bl _puts\n",
                     label,
                     label);
            strncat(text, chunk, cap - strlen(text) - 1U);
        }
        {
            char chunk[256];
            snprintf(chunk, sizeof(chunk),
                     "  ldr x30, [sp, #8]\n"
                     "  add sp, sp, #16\n"
                     "  mov w0, #%d\n"
                     "  ret\n",
                     primary->consteval_return_code);
            strncat(text, chunk, cap - strlen(text) - 1U);
        }
        if (v3_streq(plan->emit_kind, "exe")) {
            char chunk[PATH_MAX * 2];
            v3_runtime_entry_bridge_symbol(plan, bridge_symbol, sizeof(bridge_symbol));
            snprintf(chunk, sizeof(chunk),
                     ".globl %s\n"
                     "%s:\n"
                     "  b %s\n",
                     bridge_symbol,
                     bridge_symbol,
                     entry_symbol);
            strncat(text, chunk, cap - strlen(text) - 1U);
        }
        strncat(text, ".subsections_via_symbols\n", cap - strlen(text) - 1U);
        if (!v3_ensure_parent_dir(asm_path) || !v3_write_text_file(asm_path, text)) {
            free(text);
            return false;
        }
        free(text);
        return v3_compile_asm_object(asm_path, primary->output_path);
    }
    text = (char *)v3_xmalloc(cap);
    text[0] = '\0';
    strncat(text, ".text\n", cap - strlen(text) - 1U);
    strncat(text, ".p2align 2\n", cap - strlen(text) - 1U);
    for (i = 0; i < lowering->function_count; ++i) {
        char symbol_name[PATH_MAX];
        char chunk[PATH_MAX + 128];
        if (strcmp(lowering->functions[i].body_kind, "return_zero_i32") != 0 &&
            strcmp(lowering->functions[i].body_kind, "return_call_noarg_i32") != 0) {
            char *scalar_chunk = (char *)v3_xmalloc(1024U * 1024U);
            scalar_chunk[0] = '\0';
            if (!v3_try_emit_scalar_function(plan,
                                             lowering,
                                             &lowering->functions[i],
                                             scalar_chunk,
                                             1024U * 1024U,
                                             NULL)) {
                free(scalar_chunk);
                free(text);
                return false;
            }
            v3_text_append(text, cap, scalar_chunk);
            free(scalar_chunk);
            continue;
        }
        v3_primary_symbol_name(plan,
                               &lowering->functions[i],
                               symbol_name,
                               sizeof(symbol_name));
        if (strcmp(lowering->functions[i].body_kind, "return_zero_i32") == 0) {
            snprintf(chunk, sizeof(chunk),
                     ".globl %s\n"
                     "%s:\n"
                     "  mov w0, #0\n"
                     "  ret\n",
                     symbol_name,
                     symbol_name);
        } else {
            const V3LoweredFunctionStub *callee =
                v3_find_lowered_function_for_tail_call(lowering,
                                                       &lowering->functions[i]);
            char callee_symbol[PATH_MAX];
            if (!callee) {
                free(text);
                return false;
            }
            v3_primary_symbol_name(plan, callee, callee_symbol, sizeof(callee_symbol));
            snprintf(chunk, sizeof(chunk),
                     ".globl %s\n"
                     "%s:\n"
                     "  b %s\n",
                     symbol_name,
                     symbol_name,
                     callee_symbol);
        }
        strncat(text, chunk, cap - strlen(text) - 1U);
    }
    if (v3_streq(plan->emit_kind, "exe")) {
        const V3LoweredFunctionStub *entry_function = v3_find_entry_lowered_function(lowering);
        if (entry_function) {
            char bridge_symbol[PATH_MAX];
            char entry_symbol[PATH_MAX];
            char chunk[PATH_MAX * 2];
            v3_runtime_entry_bridge_symbol(plan, bridge_symbol, sizeof(bridge_symbol));
            v3_primary_symbol_name(plan, entry_function, entry_symbol, sizeof(entry_symbol));
            snprintf(chunk, sizeof(chunk),
                     ".globl %s\n"
                     "%s:\n"
                     "  b %s\n",
                     bridge_symbol,
                     bridge_symbol,
                     entry_symbol);
            strncat(text, chunk, cap - strlen(text) - 1U);
        }
    }
    strncat(text, ".subsections_via_symbols\n", cap - strlen(text) - 1U);
    if (!v3_ensure_parent_dir(asm_path) || !v3_write_text_file(asm_path, text)) {
        free(text);
        return false;
    }
    free(text);
    return v3_compile_asm_object(asm_path, primary->output_path);
}

static bool v3_materialize_provider_objects(const V3ObjectPlanStub *object_plan) {
    size_t i;
    if (object_plan->provider_source_count != object_plan->provider_object_count) {
        return false;
    }
    for (i = 0; i < object_plan->provider_source_count; ++i) {
        if (!v3_compile_c_object(object_plan->provider_source_paths[i].text,
                                 object_plan->provider_object_paths[i].text)) {
            return false;
        }
    }
    return true;
}

static bool v3_link_native_executable(const V3NativeLinkPlanStub *native_link,
                                      const char *output_path) {
    const char *cc = getenv("CC");
    size_t i;
    size_t cap = PATH_MAX * (size_t)(native_link->link_input_count + 4U);
    char *command;
    int rc;
    if (!cc || *cc == '\0') {
        cc = "cc";
    }
    if (!v3_ensure_parent_dir(output_path)) {
        return false;
    }
    command = (char *)v3_xmalloc(cap);
    command[0] = '\0';
    {
        char *quoted_cc = v3_shell_quote(cc);
        strncat(command, quoted_cc, cap - strlen(command) - 1U);
        free(quoted_cc);
    }
    strncat(command, " -arch arm64", cap - strlen(command) - 1U);
    for (i = 0; i < native_link->link_input_count; ++i) {
        char *quoted_input = v3_shell_quote(native_link->link_input_paths[i].text);
        strncat(command, " ", cap - strlen(command) - 1U);
        strncat(command, quoted_input, cap - strlen(command) - 1U);
        free(quoted_input);
    }
    {
        char *quoted_out = v3_shell_quote(output_path);
        strncat(command, " -o ", cap - strlen(command) - 1U);
        strncat(command, quoted_out, cap - strlen(command) - 1U);
        free(quoted_out);
    }
    rc = system(command);
    if (rc != 0) {
        fprintf(stderr, "[cheng_v3_seed] native link failed rc=%d command=%s\n", rc, command);
        free(command);
        return false;
    }
    free(command);
    return true;
}

static bool v3_collect_source_closure(V3SystemLinkPlanStub *plan,
                                      const char *workspace_root,
                                      const char *package_root,
                                      const char *package_id,
                                      const char *source_path,
                                      V3PlanPath *seen,
                                      size_t *seen_len) {
    char owner_module_path[PATH_MAX];
    char *text;
    V3PlanImportEdge *edges;
    size_t edge_len = 0U;
    size_t i;
    if (v3_plan_paths_contains(seen, *seen_len, source_path)) {
        return true;
    }
    if (!v3_path_exists_nonempty(source_path)) {
        fprintf(stderr, "[cheng_v3_seed] missing source: %s\n", source_path);
        return false;
    }
    if (*seen_len >= CHENG_V3_MAX_PLAN_PATHS) {
        fprintf(stderr, "[cheng_v3_seed] source closure too large\n");
        return false;
    }
    v3_plan_add_path(seen, seen_len, CHENG_V3_MAX_PLAN_PATHS, source_path);
    v3_plan_add_path(plan->source_closure_paths,
                     &plan->source_closure_count,
                     CHENG_V3_MAX_PLAN_PATHS,
                     source_path);
    text = v3_read_file(source_path);
    if (!text) {
        return false;
    }
    edges = (V3PlanImportEdge *)v3_xmalloc(sizeof(V3PlanImportEdge) * CHENG_V3_MAX_PLAN_PATHS);
    v3_source_path_to_module_path(workspace_root, package_root, package_id, source_path, owner_module_path, sizeof(owner_module_path));
    if (!v3_parse_import_edges(workspace_root,
                               package_root,
                               package_id,
                               owner_module_path,
                               text,
                               edges,
                               &edge_len,
                               CHENG_V3_MAX_PLAN_PATHS)) {
        free(text);
        free(edges);
        fprintf(stderr, "[cheng_v3_seed] malformed import in %s\n", source_path);
        return false;
    }
    free(text);
    for (i = 0; i < edge_len; ++i) {
        if (!edges[i].resolved) {
            plan->unresolved_import_count += 1U;
            continue;
        }
        if (!v3_collect_source_closure(plan,
                                       workspace_root,
                                       package_root,
                                       package_id,
                                       edges[i].target_source_path,
                                       seen,
                                       seen_len)) {
            free(edges);
            return false;
        }
    }
    free(edges);
    return true;
}

static bool v3_build_system_link_plan_stub(const V3BootstrapContract *contract,
                                           int argc,
                                           char **argv,
                                           V3SystemLinkPlanStub *plan) {
    const char *source_path = v3_flag_value(argc, argv, "--in");
    const char *out_path = v3_flag_value(argc, argv, "--out");
    const char *emit = v3_flag_value(argc, argv, "--emit");
    const char *target = v3_flag_value(argc, argv, "--target");
    const char *package_root_flag = v3_flag_value(argc, argv, "--root");
    char default_package_root[PATH_MAX];
    char seen_paths[1][1];
    V3PlanPath seen[CHENG_V3_MAX_PLAN_PATHS];
    size_t seen_len = 0U;
    char *text;
    V3PlanImportEdge direct_edges[CHENG_V3_MAX_PLAN_PATHS];
    size_t direct_edge_len = 0U;
    char owner_module_path[PATH_MAX];
    char compiler_entry[PATH_MAX];
    char last_component[PATH_MAX];
    const char *slash;
    memset(plan, 0, sizeof(*plan));
    (void)seen_paths;
    if (!source_path || *source_path == '\0') {
        fprintf(stderr, "[cheng_v3_seed] missing --in:<path>\n");
        return false;
    }
    if (!out_path || *out_path == '\0') {
        fprintf(stderr, "[cheng_v3_seed] missing --out:<path>\n");
        return false;
    }
    if (!emit || !v3_streq(emit, "exe")) {
        fprintf(stderr, "[cheng_v3_seed] unsupported --emit (only exe)\n");
        return false;
    }
    if (!target || *target == '\0') {
        fprintf(stderr, "[cheng_v3_seed] missing --target:<triple>\n");
        return false;
    }
    v3_default_package_root(contract, default_package_root, sizeof(default_package_root));
    v3_copy_text(plan->package_root, sizeof(plan->package_root), package_root_flag && *package_root_flag ? package_root_flag : default_package_root);
    v3_workspace_root_from_package_root(plan->package_root, plan->workspace_root, sizeof(plan->workspace_root));
    v3_package_id_from_root(plan->package_root, plan->package_id, sizeof(plan->package_id));
    v3_copy_text(plan->entry_path, sizeof(plan->entry_path), source_path);
    v3_copy_text(plan->output_path, sizeof(plan->output_path), out_path);
    v3_copy_text(plan->target_triple, sizeof(plan->target_triple), target);
    v3_copy_text(plan->emit_kind, sizeof(plan->emit_kind), emit);
    slash = strrchr(source_path, '/');
    snprintf(last_component, sizeof(last_component), "%s", slash ? slash + 1 : source_path);
    v3_strip_ext(last_component);
    v3_copy_text(plan->module_stem, sizeof(plan->module_stem), last_component);
    v3_copy_text(plan->module_kind, sizeof(plan->module_kind), "ordinary_program");
    if (!v3_path_exists_nonempty(source_path)) {
        fprintf(stderr, "[cheng_v3_seed] source file missing: %s\n", source_path);
        return false;
    }
    text = v3_read_file(source_path);
    if (!text) {
        return false;
    }
    plan->has_compiler_top_level = v3_source_has_top_level(text);
    plan->main_function_present = v3_source_has_main(text);
    if (plan->main_function_present) {
        v3_copy_text(plan->entry_symbol, sizeof(plan->entry_symbol), "main");
    }
    v3_source_path_to_module_path(plan->workspace_root,
                                  plan->package_root,
                                  plan->package_id,
                                  source_path,
                                  owner_module_path,
                                  sizeof(owner_module_path));
    if (v3_streq(owner_module_path, "cheng/v3/tooling/compiler_main")) {
        v3_copy_text(plan->module_kind, sizeof(plan->module_kind), "compiler_control_plane");
    }
    v3_plan_add_path(plan->owner_modules, &plan->owner_module_count, 8U, owner_module_path);
    v3_populate_runtime_requirements(plan);
    if (!v3_parse_import_edges(plan->workspace_root,
                               plan->package_root,
                               plan->package_id,
                               owner_module_path,
                               text,
                               direct_edges,
                               &direct_edge_len,
                               CHENG_V3_MAX_PLAN_PATHS)) {
        free(text);
        fprintf(stderr, "[cheng_v3_seed] malformed import in %s\n", source_path);
        return false;
    }
    free(text);
    plan->import_edge_count = direct_edge_len;
    if (!v3_collect_source_closure(plan,
                                   plan->workspace_root,
                                   plan->package_root,
                                   plan->package_id,
                                   source_path,
                                   seen,
                                   &seen_len)) {
        return false;
    }
    if (plan->source_closure_count <= 0U) {
        v3_plan_add_reason(plan, "source_closure_missing");
    }
    if (plan->unresolved_import_count > 0U) {
        v3_plan_add_reason(plan, "source_closure_unresolved_imports");
    }
    if (!plan->has_compiler_top_level) {
        v3_plan_add_reason(plan, "compiler_top_level_missing");
    }
    if (v3_streq(plan->emit_kind, "exe") && plan->entry_symbol[0] == '\0') {
        v3_plan_add_reason(plan, "program_entry_symbol_missing");
    }
    if (v3_streq(plan->emit_kind, "exe") && plan->runtime_target_count <= 0U) {
        v3_plan_add_reason(plan, "runtime_targets_not_lowered");
    }
    if (v3_streq(plan->emit_kind, "exe") && plan->provider_module_count <= 0U) {
        v3_plan_add_reason(plan, "runtime_provider_modules_not_selected");
    }
    v3_resolve_contract_path(contract, "compiler_entry_source", compiler_entry, sizeof(compiler_entry));
    (void)compiler_entry;
    return true;
}

static void v3_report_append(char **out, size_t *cap, size_t *used, const char *line) {
    size_t need = strlen(line) + 1U;
    if (*used + need + 1U >= *cap) {
        *cap *= 2U;
        *out = (char *)realloc(*out, *cap);
        if (!*out) {
            fprintf(stderr, "[cheng_v3_seed] out of memory while building report\n");
            exit(1);
        }
    }
    memcpy(*out + *used, line, need - 1U);
    *used += need - 1U;
    (*out)[(*used)++] = '\n';
    (*out)[*used] = '\0';
}

static void v3_report_append_raw(char **out, size_t *cap, size_t *used, const char *text) {
    size_t need = strlen(text);
    while (*used + need + 1U >= *cap) {
        *cap *= 2U;
        *out = (char *)realloc(*out, *cap);
        if (!*out) {
            fprintf(stderr, "[cheng_v3_seed] out of memory while building report\n");
            exit(1);
        }
    }
    memcpy(*out + *used, text, need);
    *used += need;
    (*out)[*used] = '\0';
}

static void v3_report_finish_line(char **out, size_t *cap, size_t *used) {
    while (*used + 2U >= *cap) {
        *cap *= 2U;
        *out = (char *)realloc(*out, *cap);
        if (!*out) {
            fprintf(stderr, "[cheng_v3_seed] out of memory while building report\n");
            exit(1);
        }
    }
    (*out)[(*used)++] = '\n';
    (*out)[*used] = '\0';
}

static void v3_report_append_csv_paths(char **out,
                                       size_t *cap,
                                       size_t *used,
                                       const char *label,
                                       const V3PlanPath *paths,
                                       size_t path_len) {
    size_t i;
    v3_report_append_raw(out, cap, used, label);
    v3_report_append_raw(out, cap, used, "=");
    if (path_len <= 0U) {
        v3_report_append_raw(out, cap, used, "-");
        v3_report_finish_line(out, cap, used);
        return;
    }
    for (i = 0; i < path_len; ++i) {
        if (i > 0U) {
            v3_report_append_raw(out, cap, used, ",");
        }
        v3_report_append_raw(out, cap, used, paths[i].text);
    }
    v3_report_finish_line(out, cap, used);
}

static void v3_report_append_csv_reasons(char **out,
                                         size_t *cap,
                                         size_t *used,
                                         const char *label,
                                         const char items[][128],
                                         size_t item_len) {
    size_t i;
    v3_report_append_raw(out, cap, used, label);
    v3_report_append_raw(out, cap, used, "=");
    if (item_len <= 0U) {
        v3_report_append_raw(out, cap, used, "-");
        v3_report_finish_line(out, cap, used);
        return;
    }
    for (i = 0; i < item_len; ++i) {
        if (i > 0U) {
            v3_report_append_raw(out, cap, used, ",");
        }
        v3_report_append_raw(out, cap, used, items[i]);
    }
    v3_report_finish_line(out, cap, used);
}

static void v3_report_append_csv_text256(char **out,
                                         size_t *cap,
                                         size_t *used,
                                         const char *label,
                                         const char items[][256],
                                         size_t item_len) {
    size_t i;
    v3_report_append_raw(out, cap, used, label);
    v3_report_append_raw(out, cap, used, "=");
    if (item_len <= 0U) {
        v3_report_append_raw(out, cap, used, "-");
        v3_report_finish_line(out, cap, used);
        return;
    }
    for (i = 0; i < item_len; ++i) {
        if (i > 0U) {
            v3_report_append_raw(out, cap, used, ",");
        }
        v3_report_append_raw(out, cap, used, items[i]);
    }
    v3_report_finish_line(out, cap, used);
}

static void v3_report_append_csv_functions(char **out,
                                           size_t *cap,
                                           size_t *used,
                                           const char *label,
                                           const V3LoweredFunctionStub *functions,
                                           size_t function_len) {
    size_t i;
    v3_report_append_raw(out, cap, used, label);
    v3_report_append_raw(out, cap, used, "=");
    if (function_len <= 0U) {
        v3_report_append_raw(out, cap, used, "-");
        v3_report_finish_line(out, cap, used);
        return;
    }
    for (i = 0; i < function_len; ++i) {
        if (i > 0U) {
            v3_report_append_raw(out, cap, used, ",");
        }
        v3_report_append_raw(out, cap, used, functions[i].symbol_text);
    }
    v3_report_finish_line(out, cap, used);
}

static void v3_report_append_csv_function_abi(char **out,
                                              size_t *cap,
                                              size_t *used,
                                              const char *label,
                                              const V3LoweredFunctionStub *functions,
                                              size_t function_len) {
    size_t i;
    char abi_summary[256];
    v3_report_append_raw(out, cap, used, label);
    v3_report_append_raw(out, cap, used, "=");
    if (function_len <= 0U) {
        v3_report_append_raw(out, cap, used, "-");
        v3_report_finish_line(out, cap, used);
        return;
    }
    for (i = 0; i < function_len; ++i) {
        if (i > 0U) {
            v3_report_append_raw(out, cap, used, ",");
        }
        v3_lowered_function_abi_summary(&functions[i], abi_summary, sizeof(abi_summary));
        v3_report_append_raw(out, cap, used, abi_summary);
    }
    v3_report_finish_line(out, cap, used);
}

static char *v3_system_link_plan_report(const V3BootstrapContract *contract,
                                        const V3SystemLinkPlanStub *plan) {
    size_t cap = 65536U;
    size_t used = 0U;
    char *out = (char *)v3_xmalloc(cap);
    char line[PATH_MAX * 2];
    char compiler_entry[PATH_MAX];
    out[0] = '\0';
    v3_resolve_contract_path(contract, "compiler_entry_source", compiler_entry, sizeof(compiler_entry));
    snprintf(line, sizeof(line), "entry=%s", plan->entry_path);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "output=%s", plan->output_path);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "target=%s", plan->target_triple);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "emit=%s", plan->emit_kind);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "workspace_root=%s", plan->workspace_root);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "package_root=%s", plan->package_root);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "package_id=%s", plan->package_id);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "module_stem=%s", plan->module_stem);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "module_kind=%s", plan->module_kind);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "source_closure_count=%zu", plan->source_closure_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_paths(&out, &cap, &used, "source_closure_paths", plan->source_closure_paths, plan->source_closure_count);
    v3_report_append_csv_paths(&out, &cap, &used, "owner_modules", plan->owner_modules, plan->owner_module_count);
    snprintf(line, sizeof(line), "import_edge_count=%zu", plan->import_edge_count);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "unresolved_import_count=%zu", plan->unresolved_import_count);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "has_compiler_top_level=%d", plan->has_compiler_top_level ? 1 : 0);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "main_function_present=%d", plan->main_function_present ? 1 : 0);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_paths(&out, &cap, &used, "runtime_targets", plan->runtime_targets, plan->runtime_target_count);
    v3_report_append_csv_paths(&out, &cap, &used, "provider_modules", plan->provider_modules, plan->provider_module_count);
    snprintf(line, sizeof(line), "entry_symbol=%s", plan->entry_symbol[0] != '\0' ? plan->entry_symbol : "-");
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_reasons(&out, &cap, &used, "missing_reasons", plan->missing_reasons, plan->missing_reason_count);
    v3_report_append(&out, &cap, &used, "parser_syntax=v3_source_stub_v1");
    v3_report_append(&out, &cap, &used, "parser_source_kind=ordinary_cheng_source");
    snprintf(line, sizeof(line), "build_entry=%s", compiler_entry);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append(&out, &cap, &used, "build_source_unit_count=17");
    v3_report_append(&out, &cap, &used, "link_mode=system_link");
    v3_report_append(&out, &cap, &used, "pipeline_stage=parser_stub_to_system_link_plan_stub");
    return out;
}

static char *v3_system_link_exec_report(const V3BootstrapContract *contract,
                                        const V3SystemLinkPlanStub *plan,
                                        const V3LoweringPlanStub *lowering,
                                        const V3PrimaryObjectPlanStub *primary,
                                        const V3ObjectPlanStub *object_plan,
                                        const V3NativeLinkPlanStub *native_link) {
    char *out = v3_system_link_plan_report(contract, plan);
    size_t used = strlen(out);
    size_t cap = used + 8192U;
    char line[256];
    char line_map_path[PATH_MAX];
    out = (char *)realloc(out, cap);
    if (!out) {
        fprintf(stderr, "[cheng_v3_seed] out of memory while extending exec report\n");
        exit(1);
    }
    snprintf(line, sizeof(line), "lowering_function_count=%zu", lowering->function_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_functions(&out, &cap, &used, "lowering_functions", lowering->functions, lowering->function_count);
    v3_report_append_csv_function_abi(&out, &cap, &used, "lowering_function_abi", lowering->functions, lowering->function_count);
    snprintf(line, sizeof(line), "lowering_entry_function=%s", plan->entry_symbol[0] != '\0' ? plan->entry_symbol : "-");
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_reasons(&out, &cap, &used, "lowering_missing_reasons", lowering->missing_reasons, lowering->missing_reason_count);
    snprintf(line, sizeof(line), "primary_object_format=%s",
             primary->object_format[0] != '\0' ? primary->object_format : "-");
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "primary_object_item_count=%zu", primary->item_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_paths(&out, &cap, &used, "primary_object_symbols", primary->symbol_names, primary->item_count);
    snprintf(line, sizeof(line), "primary_object_instruction_word_count=%zu", primary->instruction_word_count);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "primary_object_data_label_count=%zu", primary->data_label_count);
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "primary_object_reloc_count=%zu", primary->reloc_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_reasons(&out, &cap, &used, "primary_object_missing_reasons", primary->missing_reasons, primary->missing_reason_count);
    snprintf(line, sizeof(line), "primary_object_unsupported_function_count=%zu", primary->unsupported_function_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_paths(&out, &cap, &used, "primary_object_unsupported_functions",
                               primary->unsupported_function_symbols,
                               primary->unsupported_function_count);
    v3_report_append_csv_reasons(&out, &cap, &used, "primary_object_unsupported_body_kinds",
                                 primary->unsupported_body_kinds,
                                 primary->unsupported_function_count);
    v3_report_append_csv_reasons(&out, &cap, &used, "primary_object_unsupported_first_lines",
                                 primary->unsupported_first_lines,
                                 primary->unsupported_function_count);
    v3_report_append_csv_text256(&out, &cap, &used, "primary_object_unsupported_abi",
                                 primary->unsupported_abi_summaries,
                                 primary->unsupported_function_count);
    v3_report_append(&out, &cap, &used, "primary_object_pipeline_stage=lowering_plan_to_primary_object_plan");
    snprintf(line, sizeof(line), "primary_object_path=%s",
             object_plan->primary_object_path[0] != '\0' ? object_plan->primary_object_path : "-");
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "provider_object_count=%zu", object_plan->provider_object_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_paths(&out, &cap, &used, "provider_source_paths",
                               object_plan->provider_source_paths,
                               object_plan->provider_source_count);
    v3_report_append_csv_paths(&out, &cap, &used, "provider_object_paths",
                               object_plan->provider_object_paths,
                               object_plan->provider_object_count);
    snprintf(line, sizeof(line), "object_link_input_count=%zu", object_plan->link_input_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_paths(&out, &cap, &used, "object_link_inputs",
                               object_plan->link_input_paths,
                               object_plan->link_input_count);
    v3_report_append_csv_reasons(&out, &cap, &used, "object_missing_reasons",
                                 object_plan->missing_reasons,
                                 object_plan->missing_reason_count);
    v3_report_append(&out, &cap, &used, "object_pipeline_stage=primary_object_plan_to_object_plan");
    snprintf(line, sizeof(line), "native_linker_program=%s",
             native_link->linker_program[0] != '\0' ? native_link->linker_program : "-");
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "native_linker_flavor=%s",
             native_link->linker_flavor[0] != '\0' ? native_link->linker_flavor : "-");
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "native_output_kind=%s",
             native_link->output_kind[0] != '\0' ? native_link->output_kind : "-");
    v3_report_append(&out, &cap, &used, line);
    if (v3_streq(plan->emit_kind, "exe") && plan->output_path[0] != '\0') {
        v3_line_map_path_for_output(plan->output_path, line_map_path, sizeof(line_map_path));
        snprintf(line, sizeof(line), "debug_line_map_path=%s", line_map_path);
        v3_report_append(&out, &cap, &used, line);
    }
    snprintf(line, sizeof(line), "native_primary_object=%s",
             native_link->primary_object_path[0] != '\0' ? native_link->primary_object_path : "-");
    v3_report_append(&out, &cap, &used, line);
    snprintf(line, sizeof(line), "native_link_input_count=%zu", native_link->link_input_count);
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_paths(&out, &cap, &used, "native_link_inputs",
                               native_link->link_input_paths,
                               native_link->link_input_count);
    snprintf(line, sizeof(line), "native_link_entry_symbol=%s",
             native_link->entry_symbol[0] != '\0' ? native_link->entry_symbol : "-");
    v3_report_append(&out, &cap, &used, line);
    v3_report_append_csv_reasons(&out, &cap, &used, "native_link_missing_reasons",
                                 native_link->missing_reasons,
                                 native_link->missing_reason_count);
    v3_report_append(&out, &cap, &used, "native_link_pipeline_stage=primary_object_plan_to_native_link_plan");
    v3_report_append(&out, &cap, &used, "exec_pipeline_stage=primary_object_plan_to_native_link_plan");
    return out;
}

static bool v3_load_contract_from_cli(int argc, char **argv, V3BootstrapContract *contract) {
    const char *in_path = v3_flag_value(argc, argv, "--in");
    if (in_path && *in_path != '\0') {
        return v3_parse_contract_file(contract, in_path);
    }
    if (CHENG_V3_EMBEDDED_CONTRACT_TEXT[0] != '\0') {
        return v3_parse_contract_text(contract,
                                      CHENG_V3_EMBEDDED_SOURCE_PATH[0] != '\0' ? CHENG_V3_EMBEDDED_SOURCE_PATH : "<embedded>",
                                      CHENG_V3_EMBEDDED_CONTRACT_TEXT);
    }
    fprintf(stderr, "[cheng_v3_seed] missing --in:<path>\n");
    return false;
}

static bool v3_load_runtime_contract(int argc, char **argv, V3BootstrapContract *contract) {
    const char *contract_path = v3_flag_value(argc, argv, "--contract-in");
    if (contract_path && *contract_path != '\0') {
        return v3_parse_contract_file(contract, contract_path);
    }
    if (CHENG_V3_EMBEDDED_CONTRACT_TEXT[0] != '\0') {
        return v3_parse_contract_text(contract,
                                      CHENG_V3_EMBEDDED_SOURCE_PATH[0] != '\0' ? CHENG_V3_EMBEDDED_SOURCE_PATH : "<embedded>",
                                      CHENG_V3_EMBEDDED_CONTRACT_TEXT);
    }
    fprintf(stderr, "[cheng_v3_seed] missing embedded bootstrap contract\n");
    return false;
}

static int v3_cmd_print_contract(int argc, char **argv) {
    V3BootstrapContract contract;
    char *normalized;
    if (!v3_load_contract_from_cli(argc, argv, &contract)) {
        return 1;
    }
    if (!v3_validate_contract(&contract)) {
        v3_contract_free(&contract);
        return 1;
    }
    normalized = v3_contract_normalized_text(&contract);
    if (!normalized) {
        v3_contract_free(&contract);
        return 1;
    }
    fputs(normalized, stdout);
    free(normalized);
    v3_contract_free(&contract);
    return 0;
}

static int v3_cmd_self_check(int argc, char **argv) {
    V3BootstrapContract contract;
    char *normalized;
    uint64_t hash;
    if (!v3_load_contract_from_cli(argc, argv, &contract)) {
        return 1;
    }
    if (!v3_validate_contract(&contract)) {
        v3_contract_free(&contract);
        return 1;
    }
    normalized = v3_contract_normalized_text(&contract);
    if (!normalized) {
        v3_contract_free(&contract);
        return 1;
    }
    hash = v3_fnv1a64(normalized);
    printf("bootstrap_name=%s\n", v3_contract_get(&contract, "bootstrap_name"));
    printf("target=%s\n", v3_contract_get(&contract, "target"));
    printf("compiler_parallel_model=%s\n", v3_contract_get(&contract, "compiler_parallel_model"));
    printf("program_parallel_model=%s\n", v3_contract_get(&contract, "program_parallel_model"));
    printf("contract_hash=%016llx\n", (unsigned long long)hash);
    puts("cheng_v3_bootstrap_self_check=ok");
    free(normalized);
    v3_contract_free(&contract);
    return 0;
}

static int v3_cmd_compile_bootstrap(int argc, char **argv) {
    V3BootstrapContract contract;
    const char *out_path = v3_flag_value(argc, argv, "--out");
    const char *report_path = v3_flag_value(argc, argv, "--report-out");
    char *normalized;
    char wrapper_path[PATH_MAX];
    if (!out_path || *out_path == '\0') {
        fprintf(stderr, "[cheng_v3_seed] missing --out:<path>\n");
        return 1;
    }
    if (!v3_load_contract_from_cli(argc, argv, &contract)) {
        return 1;
    }
    if (!v3_validate_contract(&contract)) {
        v3_contract_free(&contract);
        return 1;
    }
    normalized = v3_contract_normalized_text(&contract);
    if (!normalized) {
        v3_contract_free(&contract);
        return 1;
    }
    snprintf(wrapper_path, sizeof(wrapper_path), "%s.generated.c", out_path);
    if (!v3_write_wrapper_source(wrapper_path, contract.source_path, normalized)) {
        free(normalized);
        v3_contract_free(&contract);
        return 1;
    }
    if (!v3_ensure_parent_dir(out_path) || !v3_compile_wrapper(wrapper_path, out_path)) {
        free(normalized);
        v3_contract_free(&contract);
        return 1;
    }
    if (report_path && *report_path != '\0') {
        if (!v3_write_compile_report(report_path, &contract, contract.source_path, out_path, normalized)) {
            free(normalized);
            v3_contract_free(&contract);
            return 1;
        }
    }
    printf("compiled=%s\n", out_path);
    printf("contract_hash=%016llx\n", (unsigned long long)v3_fnv1a64(normalized));
    free(normalized);
    v3_contract_free(&contract);
    return 0;
}

static int v3_cmd_status(int argc, char **argv) {
    V3BootstrapContract contract;
    char compiler_entry[PATH_MAX];
    if (!v3_load_runtime_contract(argc, argv, &contract)) {
        return 1;
    }
    if (!v3_validate_contract(&contract)) {
        v3_contract_free(&contract);
        return 1;
    }
    v3_resolve_contract_path(&contract, "compiler_entry_source", compiler_entry, sizeof(compiler_entry));
    puts("cheng_v3c");
    puts("version=v3.compiler_runtime.v1");
    puts("execution=argv_control_plane");
    puts("bootstrap_mode=v3_seed");
    printf("compiler_entry=%s\n", compiler_entry);
    printf("ordinary_command=%s\n", v3_contract_get(&contract, "ordinary_command"));
    printf("ordinary_pipeline=%s\n", v3_contract_get(&contract, "ordinary_pipeline_state"));
    v3_contract_free(&contract);
    return 0;
}

static int v3_cmd_print_build_plan(int argc, char **argv) {
    V3BootstrapContract contract;
    char workspace_root[PATH_MAX];
    char stage2_path[PATH_MAX];
    char output_path[PATH_MAX];
    char resolved[PATH_MAX];
    char seed_source[PATH_MAX];
    const char *keys[] = {
        "compiler_entry_source",
        "compiler_runtime_source",
        "compiler_request_source",
        "lang_parser_source",
        "backend_system_link_plan_source",
        "backend_lowering_plan_source",
        "backend_primary_object_plan_source",
        "backend_object_plan_source",
        "backend_native_link_plan_source",
        "backend_system_link_exec_source",
        "runtime_core_runtime_source",
        "runtime_compiler_runtime_source",
        "tooling_bootstrap_contract_source",
        "backend_build_plan_source",
        "tooling_build_driver_script"
    };
    const char *labels[] = {
        "compiler_entry_source",
        "compiler_runtime_source",
        "compiler_request_source",
        "lang_parser_source",
        "backend_system_link_plan_source",
        "backend_lowering_plan_source",
        "backend_primary_object_plan_source",
        "backend_object_plan_source",
        "backend_native_link_plan_source",
        "backend_system_link_exec_source",
        "runtime_core_runtime_source",
        "runtime_compiler_runtime_source",
        "tooling_source",
        "tooling_source",
        "tooling_source"
    };
    size_t i;
    if (!v3_load_runtime_contract(argc, argv, &contract)) {
        return 1;
    }
    if (!v3_validate_contract(&contract)) {
        v3_contract_free(&contract);
        return 1;
    }
    v3_contract_workspace_root(&contract, workspace_root, sizeof(workspace_root));
    v3_join_path(stage2_path, sizeof(stage2_path), workspace_root, "artifacts/v3_bootstrap/cheng.stage2");
    v3_join_path(output_path, sizeof(output_path), workspace_root, "artifacts/v3_backend_driver/cheng");
    v3_join_path(seed_source, sizeof(seed_source), workspace_root, "v3/bootstrap/cheng_v3_seed.c");
    printf("target=%s\n", v3_contract_get(&contract, "target"));
    puts("linker=system_link");
    printf("stage2_compiler=%s\n", stage2_path);
    v3_resolve_contract_path(&contract, "compiler_entry_source", resolved, sizeof(resolved));
    printf("entry=%s\n", resolved);
    printf("output=%s\n", output_path);
    printf("source[0]=bootstrap_contract_source:%s\n", contract.source_path);
    printf("source[1]=seed_source:%s\n", seed_source);
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        v3_resolve_contract_path(&contract, keys[i], resolved, sizeof(resolved));
        printf("source[%zu]=%s:%s\n", i + 2U, labels[i], resolved);
    }
    v3_contract_free(&contract);
    return 0;
}

static int v3_cmd_system_link_exec(int argc, char **argv) {
    V3BootstrapContract contract;
    V3SystemLinkPlanStub plan;
    V3LoweringPlanStub *lowering = NULL;
    V3PrimaryObjectPlanStub primary;
    V3ObjectPlanStub object_plan;
    V3NativeLinkPlanStub native_link;
    const char *report_path = v3_flag_value(argc, argv, "--report-out");
    char *report;
    int rc = 2;
    bool pipeline_ready;
    if (!v3_load_runtime_contract(argc, argv, &contract)) {
        return 1;
    }
    if (!v3_validate_contract(&contract)) {
        v3_contract_free(&contract);
        return 1;
    }
    if (!v3_build_system_link_plan_stub(&contract, argc, argv, &plan)) {
        v3_contract_free(&contract);
        return 1;
    }
    lowering = (V3LoweringPlanStub *)v3_xmalloc(sizeof(V3LoweringPlanStub));
    if (!v3_build_lowering_plan_stub(&plan, lowering)) {
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    }
    if (!v3_build_primary_object_plan_stub(&plan, lowering, &primary)) {
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    }
    if (!v3_build_object_plan_stub(&plan, lowering, &primary, &object_plan)) {
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    }
    if (!v3_build_native_link_plan_stub(&plan, &object_plan, &native_link)) {
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    }
    if (object_plan.provider_source_count > 0U && !v3_materialize_provider_objects(&object_plan)) {
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    }
    if (primary.missing_reason_count <= 0U &&
        !v3_materialize_primary_object(&plan, lowering, &primary)) {
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    }
    report = v3_system_link_exec_report(&contract, &plan, lowering, &primary, &object_plan, &native_link);
    if (report_path && *report_path != '\0') {
        if (!v3_ensure_parent_dir(report_path) || !v3_write_text_file(report_path, report)) {
            free(report);
            free(lowering);
            v3_contract_free(&contract);
            return 1;
        }
    }
    pipeline_ready = plan.missing_reason_count <= 0U &&
                     lowering->missing_reason_count <= 0U &&
                     primary.missing_reason_count <= 0U &&
                     object_plan.missing_reason_count <= 0U &&
                     native_link.missing_reason_count <= 0U;
    if (primary.missing_reason_count > 0U) {
        if (v3_plan_reasons_contains(primary.missing_reasons,
                                     primary.missing_reason_count,
                                     "primary_object_body_semantics_missing")) {
            fprintf(stderr, "v3 compiler: primary object body semantics missing\n");
            if (primary.unsupported_function_count > 0U) {
                fprintf(stderr,
                        "v3 compiler: first unsupported function=%s body_kind=%s abi=%s first_line=%s\n",
                        primary.unsupported_function_symbols[0].text,
                        primary.unsupported_body_kinds[0],
                        primary.unsupported_abi_summaries[0],
                        primary.unsupported_first_lines[0]);
            }
        } else if (v3_plan_reasons_contains(primary.missing_reasons,
                                            primary.missing_reason_count,
                                            "primary_object_call_target_missing")) {
            fprintf(stderr, "v3 compiler: primary object call target missing\n");
        } else {
            fprintf(stderr, "v3 compiler: primary object machine code not emitted\n");
        }
    } else if (!pipeline_ready) {
        fprintf(stderr, "v3 compiler: ordinary pipeline blocked\n");
    } else if (!v3_link_native_executable(&native_link, plan.output_path)) {
        free(report);
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    } else if (!v3_write_executable_line_map(&plan, lowering)) {
        free(report);
        free(lowering);
        v3_contract_free(&contract);
        return 1;
    } else {
        rc = 0;
    }
    fputs(report, stderr);
    free(report);
    free(lowering);
    v3_contract_free(&contract);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        v3_usage();
        return 1;
    }
    if (v3_streq(argv[1], "print-contract")) {
        return v3_cmd_print_contract(argc, argv);
    }
    if (v3_streq(argv[1], "self-check")) {
        return v3_cmd_self_check(argc, argv);
    }
    if (v3_streq(argv[1], "status")) {
        return v3_cmd_status(argc, argv);
    }
    if (v3_streq(argv[1], "print-build-plan")) {
        return v3_cmd_print_build_plan(argc, argv);
    }
    if (v3_streq(argv[1], "system-link-exec")) {
        return v3_cmd_system_link_exec(argc, argv);
    }
    if (v3_streq(argv[1], "compile-bootstrap")) {
        return v3_cmd_compile_bootstrap(argc, argv);
    }
    if (v3_streq(argv[1], "--help") || v3_streq(argv[1], "-h") || v3_streq(argv[1], "help")) {
        v3_usage();
        return 0;
    }
    fprintf(stderr, "[cheng_v3_seed] unknown command: %s\n", argv[1]);
    v3_usage();
    return 2;
}
