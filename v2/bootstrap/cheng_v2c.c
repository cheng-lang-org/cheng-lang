#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cheng_v2c_tooling.h"

typedef struct {
    size_t len;
    size_t cap;
    char **kinds;
    char **names;
    char **signatures;
    char **annotations;
    char **contracts;
    char **runtime_bindings;
    char **targets;
} SurfaceProgram;

typedef struct {
    size_t len;
    size_t cap;
    int *decl_ids;
    char **high_ops;
    char **proof_domains;
    char **runtime_bindings;
    char **effect_classes;
    char **network_modes;
    char **capability_kinds;
} SemanticFacts;

typedef struct {
    size_t len;
    size_t cap;
    int *decl_ids;
    char **op_names;
    char **names;
    char **signatures;
    char **annotations;
    char **contracts;
    char **runtime_bindings;
    char **proof_domains;
    char **effect_classes;
    char **network_modes;
    char **capability_kinds;
    char **targets;
} HighGraph;

typedef struct {
    size_t len;
    size_t cap;
    int *decl_ids;
    char **canonical_ops;
    char **runtime_targets;
    char **effect_classes;
    char **proof_domains;
    char **network_modes;
    char **capability_kinds;
} LowPlan;

static unsigned long long fnv1a64_update(unsigned long long h, const unsigned char *buf, size_t len);
static unsigned long long fnv1a64_text(unsigned long long h, const char *text);
static unsigned long long compute_pipeline_hash(const SurfaceProgram *program,
                                                const SemanticFacts *facts,
                                                const LowPlan *plan);

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

static bool starts_with(const char *text, const char *prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

static char *read_file_text(const char *path) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long size;
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

static void surface_reserve(SurfaceProgram *p, size_t need) {
    if (need <= p->cap) {
        return;
    }
    size_t next = p->cap == 0 ? 8 : p->cap * 2;
    while (next < need) {
        next *= 2;
    }
    p->kinds = (char **)xrealloc(p->kinds, next * sizeof(char *));
    p->names = (char **)xrealloc(p->names, next * sizeof(char *));
    p->signatures = (char **)xrealloc(p->signatures, next * sizeof(char *));
    p->annotations = (char **)xrealloc(p->annotations, next * sizeof(char *));
    p->contracts = (char **)xrealloc(p->contracts, next * sizeof(char *));
    p->runtime_bindings = (char **)xrealloc(p->runtime_bindings, next * sizeof(char *));
    p->targets = (char **)xrealloc(p->targets, next * sizeof(char *));
    p->cap = next;
}

static void surface_push(SurfaceProgram *p,
                         const char *kind,
                         const char *name,
                         const char *signature,
                         const char *annotation,
                         const char *contract,
                         const char *runtime_binding,
                         const char *target) {
    size_t i = p->len;
    surface_reserve(p, p->len + 1);
    p->kinds[i] = xstrdup_text(kind);
    p->names[i] = xstrdup_text(name);
    p->signatures[i] = xstrdup_text(signature);
    p->annotations[i] = xstrdup_text(annotation);
    p->contracts[i] = xstrdup_text(contract);
    p->runtime_bindings[i] = xstrdup_text(runtime_binding);
    p->targets[i] = xstrdup_text(target);
    p->len += 1;
}

static void facts_reserve(SemanticFacts *f, size_t need) {
    if (need <= f->cap) {
        return;
    }
    size_t next = f->cap == 0 ? 8 : f->cap * 2;
    while (next < need) {
        next *= 2;
    }
    f->decl_ids = (int *)xrealloc(f->decl_ids, next * sizeof(int));
    f->high_ops = (char **)xrealloc(f->high_ops, next * sizeof(char *));
    f->proof_domains = (char **)xrealloc(f->proof_domains, next * sizeof(char *));
    f->runtime_bindings = (char **)xrealloc(f->runtime_bindings, next * sizeof(char *));
    f->effect_classes = (char **)xrealloc(f->effect_classes, next * sizeof(char *));
    f->network_modes = (char **)xrealloc(f->network_modes, next * sizeof(char *));
    f->capability_kinds = (char **)xrealloc(f->capability_kinds, next * sizeof(char *));
    f->cap = next;
}

static void facts_push(SemanticFacts *f,
                       int decl_id,
                       const char *high_op,
                       const char *proof_domain,
                       const char *runtime_binding,
                       const char *effect_class,
                       const char *network_mode,
                       const char *capability_kind) {
    size_t i = f->len;
    facts_reserve(f, f->len + 1);
    f->decl_ids[i] = decl_id;
    f->high_ops[i] = xstrdup_text(high_op);
    f->proof_domains[i] = xstrdup_text(proof_domain);
    f->runtime_bindings[i] = xstrdup_text(runtime_binding);
    f->effect_classes[i] = xstrdup_text(effect_class);
    f->network_modes[i] = xstrdup_text(network_mode);
    f->capability_kinds[i] = xstrdup_text(capability_kind);
    f->len += 1;
}

static void high_reserve(HighGraph *g, size_t need) {
    if (need <= g->cap) {
        return;
    }
    size_t next = g->cap == 0 ? 8 : g->cap * 2;
    while (next < need) {
        next *= 2;
    }
    g->decl_ids = (int *)xrealloc(g->decl_ids, next * sizeof(int));
    g->op_names = (char **)xrealloc(g->op_names, next * sizeof(char *));
    g->names = (char **)xrealloc(g->names, next * sizeof(char *));
    g->signatures = (char **)xrealloc(g->signatures, next * sizeof(char *));
    g->annotations = (char **)xrealloc(g->annotations, next * sizeof(char *));
    g->contracts = (char **)xrealloc(g->contracts, next * sizeof(char *));
    g->runtime_bindings = (char **)xrealloc(g->runtime_bindings, next * sizeof(char *));
    g->proof_domains = (char **)xrealloc(g->proof_domains, next * sizeof(char *));
    g->effect_classes = (char **)xrealloc(g->effect_classes, next * sizeof(char *));
    g->network_modes = (char **)xrealloc(g->network_modes, next * sizeof(char *));
    g->capability_kinds = (char **)xrealloc(g->capability_kinds, next * sizeof(char *));
    g->targets = (char **)xrealloc(g->targets, next * sizeof(char *));
    g->cap = next;
}

static void high_push(HighGraph *g,
                      int decl_id,
                      const char *op_name,
                      const char *name,
                      const char *signature,
                      const char *annotation,
                      const char *contract,
                      const char *runtime_binding,
                      const char *proof_domain,
                      const char *effect_class,
                      const char *network_mode,
                      const char *capability_kind,
                      const char *target) {
    size_t i = g->len;
    high_reserve(g, g->len + 1);
    g->decl_ids[i] = decl_id;
    g->op_names[i] = xstrdup_text(op_name);
    g->names[i] = xstrdup_text(name);
    g->signatures[i] = xstrdup_text(signature);
    g->annotations[i] = xstrdup_text(annotation);
    g->contracts[i] = xstrdup_text(contract);
    g->runtime_bindings[i] = xstrdup_text(runtime_binding);
    g->proof_domains[i] = xstrdup_text(proof_domain);
    g->effect_classes[i] = xstrdup_text(effect_class);
    g->network_modes[i] = xstrdup_text(network_mode);
    g->capability_kinds[i] = xstrdup_text(capability_kind);
    g->targets[i] = xstrdup_text(target);
    g->len += 1;
}

static void low_reserve(LowPlan *p, size_t need) {
    if (need <= p->cap) {
        return;
    }
    size_t next = p->cap == 0 ? 8 : p->cap * 2;
    while (next < need) {
        next *= 2;
    }
    p->decl_ids = (int *)xrealloc(p->decl_ids, next * sizeof(int));
    p->canonical_ops = (char **)xrealloc(p->canonical_ops, next * sizeof(char *));
    p->runtime_targets = (char **)xrealloc(p->runtime_targets, next * sizeof(char *));
    p->effect_classes = (char **)xrealloc(p->effect_classes, next * sizeof(char *));
    p->proof_domains = (char **)xrealloc(p->proof_domains, next * sizeof(char *));
    p->network_modes = (char **)xrealloc(p->network_modes, next * sizeof(char *));
    p->capability_kinds = (char **)xrealloc(p->capability_kinds, next * sizeof(char *));
    p->cap = next;
}

static void low_push(LowPlan *p,
                     int decl_id,
                     const char *canonical_op,
                     const char *runtime_target,
                     const char *effect_class,
                     const char *proof_domain,
                     const char *network_mode,
                     const char *capability_kind) {
    size_t i = p->len;
    low_reserve(p, p->len + 1);
    p->decl_ids[i] = decl_id;
    p->canonical_ops[i] = xstrdup_text(canonical_op);
    p->runtime_targets[i] = xstrdup_text(runtime_target);
    p->effect_classes[i] = xstrdup_text(effect_class);
    p->proof_domains[i] = xstrdup_text(proof_domain);
    p->network_modes[i] = xstrdup_text(network_mode);
    p->capability_kinds[i] = xstrdup_text(capability_kind);
    p->len += 1;
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

static char *parse_name_after_prefix(const char *line, const char *prefix) {
    const char *start = line + strlen(prefix);
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }
    return xstrdup_range(start, parse_name_end(start));
}

static void parse_line_into_surface(SurfaceProgram *program, const char *line, int line_no) {
    char *trimmed = dup_trimmed_line(line);
    const char *colon;
    if (trimmed[0] == '\0' || starts_with(trimmed, "//")) {
        free(trimmed);
        return;
    }
    if (starts_with(trimmed, "asset ")) {
        char *name = parse_name_after_prefix(trimmed, "asset ");
        colon = strchr(trimmed, ':');
        if (colon == NULL) {
            surface_push(program,
                         "asset_decl",
                         name,
                         trimmed,
                         "",
                         "",
                         "runtime/unimaker.verify_asset_contract",
                         "");
        } else {
            char *contract = trim_copy(colon + 1, trimmed + strlen(trimmed));
            surface_push(program,
                         "asset_decl",
                         name,
                         trimmed,
                         "",
                         contract,
                         "runtime/unimaker.verify_asset_contract",
                         "");
            free(contract);
        }
        free(name);
        free(trimmed);
        return;
    }
    if (starts_with(trimmed, "actor ")) {
        char *name = parse_name_after_prefix(trimmed, "actor ");
        surface_push(program,
                     "actor_decl",
                     name,
                     trimmed,
                     "",
                     "",
                     "runtime/unimaker.spawn_actor",
                     "");
        free(name);
        free(trimmed);
        return;
    }
    if (starts_with(trimmed, "@ganzhi state ")) {
        char *name = parse_name_after_prefix(trimmed, "@ganzhi state ");
        surface_push(program,
                     "ganzhi_state_decl",
                     name,
                     trimmed,
                     "@ganzhi",
                     "",
                     "runtime/unimaker.register_ganzhi_state",
                     "");
        free(name);
        free(trimmed);
        return;
    }
    if (starts_with(trimmed, "@nfc_callback fn ")) {
        char *name = parse_name_after_prefix(trimmed, "@nfc_callback fn ");
        surface_push(program,
                     "hardware_callback_decl",
                     name,
                     trimmed,
                     "@nfc_callback",
                     "",
                     "runtime/unimaker.bind_nfc_callback",
                     "");
        free(name);
        free(trimmed);
        return;
    }
    if (starts_with(trimmed, "@evolve(")) {
        const char *close = strchr(trimmed, ')');
        const char *fn_pos;
        char *target;
        char *name;
        if (close == NULL) {
            fprintf(stderr, "[cheng_v2c] line %d: malformed @evolve annotation\n", line_no);
            exit(1);
        }
        fn_pos = strstr(close + 1, "fn ");
        if (fn_pos == NULL) {
            fprintf(stderr, "[cheng_v2c] line %d: missing fn after @evolve\n", line_no);
            exit(1);
        }
        target = trim_copy(trimmed + strlen("@evolve("), close);
        name = parse_name_after_prefix(fn_pos, "fn ");
        surface_push(program,
                     "evolve_decl",
                     name,
                     trimmed,
                     "@evolve",
                     "",
                     "runtime/unimaker.register_evolve_site",
                     target);
        free(target);
        free(name);
        free(trimmed);
        return;
    }
    fprintf(stderr, "[cheng_v2c] line %d: unsupported declaration: %s\n", line_no, trimmed);
    exit(1);
}

static SurfaceProgram parse_surface_program(const char *path) {
    char *text = read_file_text(path);
    SurfaceProgram out;
    char *cursor = text;
    int line_no = 1;
    memset(&out, 0, sizeof(out));
    while (*cursor != '\0') {
        char *line_start = cursor;
        char *line_end = cursor;
        char saved;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        saved = *line_end;
        *line_end = '\0';
        parse_line_into_surface(&out, line_start, line_no);
        *line_end = saved;
        if (*line_end == '\n') {
            cursor = line_end + 1;
            line_no += 1;
        } else {
            cursor = line_end;
        }
    }
    free(text);
    return out;
}

static const char *skip_spaces(const char *p) {
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static char *parse_quoted_string(const char **pp) {
    const char *p = skip_spaces(*pp);
    char *out;
    size_t cap = 32;
    size_t len = 0;
    if (*p != '"') {
        die("stage1 parse: expected string literal");
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
    if (*p != '"') {
        die("stage1 parse: unterminated string literal");
    }
    ++p;
    *pp = p;
    return out;
}

static void parse_stage1_constructor_line(SurfaceProgram *program, const char *line) {
    char *trimmed = dup_trimmed_line(line);
    const char *ctor;
    const char *p;
    char *arg0 = NULL;
    char *arg1 = NULL;
    char *arg2 = NULL;
    if (trimmed[0] == '\0' || starts_with(trimmed, "import ") || starts_with(trimmed, "fn ") ||
        starts_with(trimmed, "var ") || starts_with(trimmed, "return ") || strcmp(trimmed, "    return out") == 0 ||
        strcmp(trimmed, "return out") == 0 || trimmed[0] == '#') {
        free(trimmed);
        return;
    }
    ctor = strstr(trimmed, "usurf.");
    if (ctor == NULL) {
        free(trimmed);
        return;
    }
    ctor += strlen("usurf.");
    p = strchr(ctor, '(');
    if (p == NULL) {
        free(trimmed);
        die("stage1 parse: malformed constructor line");
    }
    if (strncmp(ctor, "assetDecl", 9) == 0) {
        p += 1;
        arg0 = parse_quoted_string(&p);
        p = skip_spaces(p);
        if (*p == ',') {
            ++p;
        }
        arg1 = parse_quoted_string(&p);
        surface_push(program, "asset_decl", arg0, "asset", "", arg1, "runtime/unimaker.verify_asset_contract", "");
    } else if (strncmp(ctor, "actorDecl", 9) == 0) {
        p += 1;
        arg0 = parse_quoted_string(&p);
        p = skip_spaces(p);
        if (*p == ',') {
            ++p;
        }
        arg1 = parse_quoted_string(&p);
        surface_push(program, "actor_decl", arg0, arg1, "", "", "runtime/unimaker.spawn_actor", "");
    } else if (strncmp(ctor, "ganzhiStateDecl", 15) == 0) {
        p += 1;
        arg0 = parse_quoted_string(&p);
        p = skip_spaces(p);
        if (*p == ',') {
            ++p;
        }
        arg1 = parse_quoted_string(&p);
        surface_push(program, "ganzhi_state_decl", arg0, arg1, "@ganzhi", "", "runtime/unimaker.register_ganzhi_state", "");
    } else if (strncmp(ctor, "callbackDecl", 12) == 0) {
        p += 1;
        arg0 = parse_quoted_string(&p);
        p = skip_spaces(p);
        if (*p == ',') {
            ++p;
        }
        arg1 = parse_quoted_string(&p);
        surface_push(program, "hardware_callback_decl", arg0, arg1, "@nfc_callback", "", "runtime/unimaker.bind_nfc_callback", "");
    } else if (strncmp(ctor, "evolveDecl", 10) == 0) {
        p += 1;
        arg0 = parse_quoted_string(&p);
        p = skip_spaces(p);
        if (*p == ',') {
            ++p;
        }
        arg1 = parse_quoted_string(&p);
        p = skip_spaces(p);
        if (*p == ',') {
            ++p;
        }
        arg2 = parse_quoted_string(&p);
        surface_push(program, "evolve_decl", arg0, arg1, "@evolve", "", "runtime/unimaker.register_evolve_site", arg2);
    } else {
        free(trimmed);
        die("stage1 parse: unknown constructor");
    }
    free(arg0);
    free(arg1);
    free(arg2);
    free(trimmed);
}

static SurfaceProgram parse_stage1_program(const char *path) {
    char *text = read_file_text(path);
    SurfaceProgram out;
    char *cursor = text;
    memset(&out, 0, sizeof(out));
    while (*cursor != '\0') {
        char *line_start = cursor;
        char *line_end = cursor;
        char saved;
        while (*line_end != '\0' && *line_end != '\n') {
            ++line_end;
        }
        saved = *line_end;
        *line_end = '\0';
        parse_stage1_constructor_line(&out, line_start);
        *line_end = saved;
        cursor = *line_end == '\n' ? line_end + 1 : line_end;
    }
    free(text);
    return out;
}

static bool file_looks_like_stage1_program(const char *path) {
    char *text = read_file_text(path);
    bool out = strstr(text, "bootstrapSurfaceProgram") != NULL && strstr(text, "usurf.") != NULL;
    free(text);
    return out;
}

static SurfaceProgram parse_input_program(const char *path) {
    if (file_looks_like_stage1_program(path)) {
        return parse_stage1_program(path);
    }
    return parse_surface_program(path);
}

static SemanticFacts build_semantic_facts(const SurfaceProgram *program) {
    SemanticFacts out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < program->len; ++i) {
        const char *kind = program->kinds[i];
        if (strcmp(kind, "asset_decl") == 0) {
            facts_push(&out, (int)i, "asset_contract_def", "refinement_contract",
                       program->runtime_bindings[i], "pure_contract",
                       "content_addressed_value", "none");
        } else if (strcmp(kind, "actor_decl") == 0) {
            facts_push(&out, (int)i, "actor_def", "actor_isolation",
                       program->runtime_bindings[i], "isolated_state",
                       "addressed_actor", "none");
        } else if (strcmp(kind, "ganzhi_state_decl") == 0) {
            facts_push(&out, (int)i, "ganzhi_state_def", "ganzhi_merge",
                       program->runtime_bindings[i], "crdt_state",
                       "crdt_snapshot", "none");
        } else if (strcmp(kind, "hardware_callback_decl") == 0) {
            facts_push(&out, (int)i, "hardware_callback_def", "capability_contract",
                       program->runtime_bindings[i], "hardware_interrupt",
                       "local_capability_entry", "nfc");
        } else {
            facts_push(&out, (int)i, "evolve_site_def", "evolve_candidate",
                       program->runtime_bindings[i], "agentic_candidate",
                       "candidate_rule_pack", "npu");
        }
    }
    return out;
}

static HighGraph build_high_graph(const SurfaceProgram *program, const SemanticFacts *facts) {
    HighGraph out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < program->len; ++i) {
        high_push(&out,
                  facts->decl_ids[i],
                  facts->high_ops[i],
                  program->names[i],
                  program->signatures[i],
                  program->annotations[i],
                  program->contracts[i],
                  facts->runtime_bindings[i],
                  facts->proof_domains[i],
                  facts->effect_classes[i],
                  facts->network_modes[i],
                  facts->capability_kinds[i],
                  program->targets[i]);
    }
    return out;
}

static void lower_unimaker_op(const char *op_name,
                              const char **runtime_target,
                              const char **effect_class,
                              const char **proof_domain) {
    if (strcmp(op_name, "asset_contract_def") == 0) {
        *runtime_target = "runtime/unimaker.verify_asset_contract";
        *effect_class = "pure_contract";
        *proof_domain = "refinement_contract";
    } else if (strcmp(op_name, "actor_def") == 0) {
        *runtime_target = "runtime/unimaker.spawn_actor";
        *effect_class = "isolated_state";
        *proof_domain = "actor_isolation";
    } else if (strcmp(op_name, "ganzhi_state_def") == 0) {
        *runtime_target = "runtime/unimaker.register_ganzhi_state";
        *effect_class = "crdt_state";
        *proof_domain = "ganzhi_merge";
    } else if (strcmp(op_name, "hardware_callback_def") == 0) {
        *runtime_target = "runtime/unimaker.bind_nfc_callback";
        *effect_class = "hardware_interrupt";
        *proof_domain = "capability_contract";
    } else if (strcmp(op_name, "evolve_site_def") == 0) {
        *runtime_target = "runtime/unimaker.register_evolve_site";
        *effect_class = "agentic_candidate";
        *proof_domain = "evolve_candidate";
    } else {
        die("unknown unimaker op");
    }
}

static LowPlan build_low_plan(const HighGraph *graph) {
    LowPlan out;
    size_t i;
    memset(&out, 0, sizeof(out));
    for (i = 0; i < graph->len; ++i) {
        const char *runtime_target = NULL;
        const char *effect_class = NULL;
        const char *proof_domain = NULL;
        lower_unimaker_op(graph->op_names[i], &runtime_target, &effect_class, &proof_domain);
        low_push(&out,
                 graph->decl_ids[i],
                 graph->op_names[i],
                 runtime_target,
                 effect_class,
                 proof_domain,
                 graph->network_modes[i],
                 graph->capability_kinds[i]);
    }
    return out;
}

static void print_surface(const SurfaceProgram *program) {
    size_t i;
    printf("surface_version=v2.frontend.unimaker_surface_ir.v1\n");
    printf("decls=%zu\n", program->len);
    for (i = 0; i < program->len; ++i) {
        printf("%zu:%s | %s", i, program->kinds[i], program->names[i]);
        if (program->annotations[i][0] != '\0') {
            printf(" | %s", program->annotations[i]);
        }
        if (program->contracts[i][0] != '\0') {
            printf(" | %s", program->contracts[i]);
        }
        if (program->targets[i][0] != '\0') {
            printf(" | %s", program->targets[i]);
        }
        putchar('\n');
    }
}

static void print_facts(const SemanticFacts *facts) {
    size_t i;
    printf("facts_version=v2.semantic_facts.unimaker.v1\n");
    printf("facts=%zu\n", facts->len);
    for (i = 0; i < facts->len; ++i) {
        printf("%d:%s | %s | %s | %s | %s\n",
               facts->decl_ids[i],
               facts->high_ops[i],
               facts->proof_domains[i],
               facts->effect_classes[i],
               facts->network_modes[i],
               facts->capability_kinds[i]);
    }
}

static void print_high(const HighGraph *graph) {
    size_t i;
    printf("high_uir_version=v2.high_uir.unimaker_graph.v1\n");
    printf("nodes=%zu\n", graph->len);
    for (i = 0; i < graph->len; ++i) {
        printf("%d:%s | %s | %s | %s\n",
               graph->decl_ids[i],
               graph->op_names[i],
               graph->proof_domains[i],
               graph->network_modes[i],
               graph->runtime_bindings[i]);
    }
}

static void print_low(const LowPlan *plan) {
    size_t i;
    printf("low_uir_version=v2.low_uir.unimaker_lowering.v1\n");
    printf("nodes=%zu\n", plan->len);
    for (i = 0; i < plan->len; ++i) {
        printf("%d:%s | %s | %s | %s\n",
               plan->decl_ids[i],
               plan->canonical_ops[i],
               plan->runtime_targets[i],
               plan->network_modes[i],
               plan->capability_kinds[i]);
    }
}

static void print_pipeline(const SurfaceProgram *program,
                           const SemanticFacts *facts,
                           const HighGraph *graph,
                           const LowPlan *plan) {
    size_t i;
    printf("pipeline_version=v2.driver.unimaker_pipeline.v1\n");
    printf("decls=%zu\n", program->len);
    printf("facts=%zu\n", facts->len);
    printf("high_uir_nodes=%zu\n", graph->len);
    printf("low_uir_nodes=%zu\n", plan->len);
    for (i = 0; i < program->len; ++i) {
        printf("%zu:%s | %s => %s => %s => %s\n",
               i,
               program->kinds[i],
               program->names[i],
               facts->high_ops[i],
               plan->canonical_ops[i],
               plan->runtime_targets[i]);
    }
}

static void emit_c_escaped(FILE *out, const char *text) {
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

static void sanitize_symbol(char *dst, size_t dst_cap, const char *src) {
    size_t i = 0;
    while (*src != '\0' && i + 1 < dst_cap) {
        unsigned char ch = (unsigned char)*src;
        dst[i++] = (char)(isalnum(ch) ? ch : '_');
        ++src;
    }
    dst[i] = '\0';
}

static void emit_c_stub(FILE *out, const SurfaceProgram *program) {
    size_t i;
    fprintf(out, "#include <stdio.h>\n\n");
    for (i = 0; i < program->len; ++i) {
        char sym[256];
        sanitize_symbol(sym, sizeof(sym), program->names[i]);
        if (strcmp(program->kinds[i], "asset_decl") == 0) {
            fprintf(out, "static void asset_contract_%s(void) { puts(", sym);
            emit_c_escaped(out, program->contracts[i]);
            fprintf(out, "); }\n");
        } else if (strcmp(program->kinds[i], "actor_decl") == 0) {
            fprintf(out, "static void actor_spawn_%s(void) { puts(", sym);
            emit_c_escaped(out, program->signatures[i]);
            fprintf(out, "); }\n");
        } else if (strcmp(program->kinds[i], "ganzhi_state_decl") == 0) {
            fprintf(out, "static void ganzhi_state_%s(void) { puts(", sym);
            emit_c_escaped(out, program->signatures[i]);
            fprintf(out, "); }\n");
        } else if (strcmp(program->kinds[i], "hardware_callback_decl") == 0) {
            fprintf(out, "static void nfc_callback_%s(void) { puts(", sym);
            emit_c_escaped(out, program->signatures[i]);
            fprintf(out, "); }\n");
        } else {
            fprintf(out, "static void evolve_site_%s(void) { puts(", sym);
            emit_c_escaped(out, program->targets[i]);
            fprintf(out, "); }\n");
        }
    }
    fprintf(out, "\nint main(void) {\n");
    for (i = 0; i < program->len; ++i) {
        char sym[256];
        sanitize_symbol(sym, sizeof(sym), program->names[i]);
        if (strcmp(program->kinds[i], "asset_decl") == 0) {
            fprintf(out, "    asset_contract_%s();\n", sym);
        } else if (strcmp(program->kinds[i], "actor_decl") == 0) {
            fprintf(out, "    actor_spawn_%s();\n", sym);
        } else if (strcmp(program->kinds[i], "ganzhi_state_decl") == 0) {
            fprintf(out, "    ganzhi_state_%s();\n", sym);
        } else if (strcmp(program->kinds[i], "hardware_callback_decl") == 0) {
            fprintf(out, "    nfc_callback_%s();\n", sym);
        } else {
            fprintf(out, "    evolve_site_%s();\n", sym);
        }
    }
    fprintf(out, "    return 0;\n}\n");
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

static void emit_stage1_module(FILE *out, const SurfaceProgram *program) {
    size_t i;
    fprintf(out, "import std/seqs\n");
    fprintf(out, "import compiler/frontend/unimaker_surface_ir_v2 as usurf\n");
    fprintf(out, "import compiler/driver/unimaker_pipeline_v2 as upipe\n\n");
    fprintf(out, "fn bootstrapSurfaceProgram(): usurf.UnimakerSurfaceProgram =\n");
    fprintf(out, "    var out: usurf.UnimakerSurfaceProgram = usurf.newUnimakerSurfaceProgram()\n");
    for (i = 0; i < program->len; ++i) {
        char *stage1_sig = NULL;
        fprintf(out, "    add(out.decls, usurf.");
        if (strcmp(program->kinds[i], "asset_decl") == 0) {
            fprintf(out, "assetDecl(");
            emit_cheng_escaped(out, program->names[i]);
            fprintf(out, ", ");
            emit_cheng_escaped(out, program->contracts[i]);
            fprintf(out, "))\n");
        } else if (strcmp(program->kinds[i], "actor_decl") == 0) {
            fprintf(out, "actorDecl(");
            emit_cheng_escaped(out, program->names[i]);
            fprintf(out, ", ");
            emit_cheng_escaped(out, program->signatures[i]);
            fprintf(out, "))\n");
        } else if (strcmp(program->kinds[i], "ganzhi_state_decl") == 0) {
            const char *sig = program->signatures[i];
            if (starts_with(sig, "@ganzhi ")) {
                sig += strlen("@ganzhi ");
            }
            stage1_sig = xstrdup_text(sig);
            fprintf(out, "ganzhiStateDecl(");
            emit_cheng_escaped(out, program->names[i]);
            fprintf(out, ", ");
            emit_cheng_escaped(out, stage1_sig);
            fprintf(out, "))\n");
        } else if (strcmp(program->kinds[i], "hardware_callback_decl") == 0) {
            const char *sig = program->signatures[i];
            if (starts_with(sig, "@nfc_callback ")) {
                sig += strlen("@nfc_callback ");
            }
            stage1_sig = xstrdup_text(sig);
            fprintf(out, "callbackDecl(");
            emit_cheng_escaped(out, program->names[i]);
            fprintf(out, ", ");
            emit_cheng_escaped(out, stage1_sig);
            fprintf(out, "))\n");
        } else {
            const char *sig = program->signatures[i];
            const char *fn_pos = strstr(sig, "fn ");
            if (fn_pos != NULL) {
                sig = fn_pos;
            }
            stage1_sig = xstrdup_text(sig);
            fprintf(out, "evolveDecl(");
            emit_cheng_escaped(out, program->names[i]);
            fprintf(out, ", ");
            emit_cheng_escaped(out, stage1_sig);
            fprintf(out, ", ");
            emit_cheng_escaped(out, program->targets[i]);
            fprintf(out, "))\n");
        }
        free(stage1_sig);
    }
    fprintf(out, "    return out\n\n");
    fprintf(out, "fn bootstrapPipeline(): upipe.UnimakerPipelineBundle =\n");
    fprintf(out, "    return upipe.compileUnimakerSurfaceProgram(bootstrapSurfaceProgram())\n");
}

static unsigned long long compute_pipeline_hash(const SurfaceProgram *program,
                                                const SemanticFacts *facts,
                                                const LowPlan *plan) {
    unsigned long long h = 1469598103934665603ULL;
    size_t i;
    h = fnv1a64_text(h, "v2.driver.unimaker_pipeline.v1");
    for (i = 0; i < program->len; ++i) {
        h = fnv1a64_text(h, program->kinds[i]);
        h = fnv1a64_text(h, program->names[i]);
        h = fnv1a64_text(h, facts->high_ops[i]);
        h = fnv1a64_text(h, plan->canonical_ops[i]);
        h = fnv1a64_text(h, plan->runtime_targets[i]);
    }
    return h;
}

static void print_pipeline_hash(const SurfaceProgram *program,
                                const SemanticFacts *facts,
                                const LowPlan *plan) {
    printf("pipeline_hash_fnv1a64=%016llx\n", compute_pipeline_hash(program, facts, plan));
}

static int selfhost_check(const char *source_path) {
    char tmp_stage1_path[] = "/tmp/cheng_v2_stage1_XXXXXX";
    char tmp_stage2_path[] = "/tmp/cheng_v2_stage2_XXXXXX";
    int stage1_fd = mkstemp(tmp_stage1_path);
    int stage2_fd;
    SurfaceProgram surface_program;
    SemanticFacts surface_facts;
    HighGraph surface_graph;
    LowPlan surface_plan;
    SurfaceProgram stage1_program;
    SemanticFacts stage1_facts;
    HighGraph stage1_graph;
    LowPlan stage1_plan;
    SurfaceProgram stage2_program;
    SemanticFacts stage2_facts;
    HighGraph stage2_graph;
    LowPlan stage2_plan;
    unsigned long long surface_hash;
    unsigned long long stage1_hash;
    unsigned long long stage2_hash;
    int stage0_stage1_equal;
    int stage1_stage2_equal;
    int stage1_stage2_source_equal;
    FILE *stage1_out;
    FILE *stage2_out;
    if (stage1_fd < 0) {
        die_errno("mkstemp failed", tmp_stage1_path);
    }
    stage1_out = fdopen(stage1_fd, "wb");
    if (stage1_out == NULL) {
        close(stage1_fd);
        die_errno("fdopen failed", tmp_stage1_path);
    }
    surface_program = parse_input_program(source_path);
    surface_facts = build_semantic_facts(&surface_program);
    surface_graph = build_high_graph(&surface_program, &surface_facts);
    surface_plan = build_low_plan(&surface_graph);
    emit_stage1_module(stage1_out, &surface_program);
    fclose(stage1_out);
    stage1_program = parse_input_program(tmp_stage1_path);
    stage1_facts = build_semantic_facts(&stage1_program);
    stage1_graph = build_high_graph(&stage1_program, &stage1_facts);
    stage1_plan = build_low_plan(&stage1_graph);
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
    emit_stage1_module(stage2_out, &stage1_program);
    fclose(stage2_out);
    stage2_program = parse_input_program(tmp_stage2_path);
    stage2_facts = build_semantic_facts(&stage2_program);
    stage2_graph = build_high_graph(&stage2_program, &stage2_facts);
    stage2_plan = build_low_plan(&stage2_graph);
    surface_hash = compute_pipeline_hash(&surface_program, &surface_facts, &surface_plan);
    stage1_hash = compute_pipeline_hash(&stage1_program, &stage1_facts, &stage1_plan);
    stage2_hash = compute_pipeline_hash(&stage2_program, &stage2_facts, &stage2_plan);
    stage0_stage1_equal = surface_hash == stage1_hash ? 1 : 0;
    stage1_stage2_equal = stage1_hash == stage2_hash ? 1 : 0;
    stage1_stage2_source_equal = text_files_equal(tmp_stage1_path, tmp_stage2_path);
    printf("selfhost_stage0_hash=%016llx\n", surface_hash);
    printf("selfhost_stage1_hash=%016llx\n", stage1_hash);
    printf("selfhost_stage2_hash=%016llx\n", stage2_hash);
    printf("selfhost_stage0_stage1_equal=%d\n", stage0_stage1_equal);
    printf("selfhost_stage1_stage2_equal=%d\n", stage1_stage2_equal);
    printf("selfhost_stage1_stage2_source_equal=%d\n", stage1_stage2_source_equal);
    printf("selfhost_equal=%d\n", (stage0_stage1_equal && stage1_stage2_equal && stage1_stage2_source_equal) ? 1 : 0);
    unlink(tmp_stage1_path);
    unlink(tmp_stage2_path);
    return (stage0_stage1_equal && stage1_stage2_equal && stage1_stage2_source_equal) ? 0 : 1;
}

static void usage(void) {
    puts("cheng_v2c");
    puts("usage: cheng_v2c <command> <source> [out]");
    puts("");
    puts("commands:");
    puts("  help       show this message");
    puts("  surface    parse a minimal Unimaker source file and print the surface program");
    puts("  facts      print derived semantic facts");
    puts("  high       print the High-UIR graph");
    puts("  low        print the Low-UIR plan");
    puts("  pipeline   print the full Unimaker pipeline");
    puts("  hash-pipeline  print a deterministic FNV1a64 over the pipeline");
    puts("  emit-c     emit a host C stub for the parsed source");
    puts("  emit-stage1 emit a Cheng stage1 bootstrap module for the parsed source");
    puts("  selfhost-check  verify stage0 -> stage1 -> stage2 fixed-point closure");
    cheng_v2c_tooling_print_usage();
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

int main(int argc, char **argv) {
    const char *cmd;
    const char *source_path;
    const char *out_path;
    SurfaceProgram program;
    SemanticFacts facts;
    HighGraph graph;
    LowPlan plan;
    FILE *out;
    if (argc < 2 || strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage();
        return argc < 2 ? 0 : 0;
    }
    cmd = argv[1];
    if (cheng_v2c_tooling_is_command(cmd)) {
        return cheng_v2c_tooling_handle(argc, argv);
    }
    if (argc < 3) {
        usage();
        return 1;
    }
    source_path = argv[2];
    out_path = argc >= 4 ? argv[3] : NULL;
    program = parse_input_program(source_path);
    facts = build_semantic_facts(&program);
    graph = build_high_graph(&program, &facts);
    plan = build_low_plan(&graph);
    if (strcmp(cmd, "surface") == 0) {
        print_surface(&program);
        return 0;
    }
    if (strcmp(cmd, "facts") == 0) {
        print_facts(&facts);
        return 0;
    }
    if (strcmp(cmd, "high") == 0) {
        print_high(&graph);
        return 0;
    }
    if (strcmp(cmd, "low") == 0) {
        print_low(&plan);
        return 0;
    }
    if (strcmp(cmd, "pipeline") == 0) {
        print_pipeline(&program, &facts, &graph, &plan);
        return 0;
    }
    if (strcmp(cmd, "hash-pipeline") == 0) {
        print_pipeline_hash(&program, &facts, &plan);
        return 0;
    }
    if (strcmp(cmd, "emit-c") == 0) {
        out = out_path != NULL ? fopen(out_path, "wb") : stdout;
        if (out == NULL) {
            die_errno("fopen failed", out_path);
        }
        emit_c_stub(out, &program);
        if (out != stdout) {
            fclose(out);
        }
        return 0;
    }
    if (strcmp(cmd, "emit-stage1") == 0) {
        out = out_path != NULL ? fopen(out_path, "wb") : stdout;
        if (out == NULL) {
            die_errno("fopen failed", out_path);
        }
        emit_stage1_module(out, &program);
        if (out != stdout) {
            fclose(out);
        }
        return 0;
    }
    if (strcmp(cmd, "selfhost-check") == 0) {
        return selfhost_check(source_path);
    }
    usage();
    return 1;
}
