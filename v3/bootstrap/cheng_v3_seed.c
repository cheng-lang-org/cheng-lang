#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
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
    "user_program_auto_parallel",
    "bagua_bpi"
};

static void v3_usage(void) {
    puts("cheng_v3_seed");
    puts("usage:");
    puts("  cheng_v3_seed print-contract [--in:<path>]");
    puts("  cheng_v3_seed self-check [--in:<path>]");
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
        !v3_has_csv_token(commands, "compile-bootstrap")) {
        fprintf(stderr, "[cheng_v3_seed] supported_commands drift\n");
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
        }
    }
    return NULL;
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
