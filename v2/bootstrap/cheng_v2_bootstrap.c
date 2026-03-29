#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct RequiredPath {
    const char *path;
    int is_dir;
} RequiredPath;

static const RequiredPath k_required_paths[] = {
    {"cheng-package.toml", 0},
    {"README.md", 0},
    {"docs/architecture.md", 0},
    {"docs/bootstrap-implementation.md", 0},
    {"docs/contracts.md", 0},
    {"docs/plan.md", 0},
    {"docs/selfhost.md", 0},
    {"docs/strings.md", 0},
    {"examples/network_distribution_module.cheng", 0},
    {"examples/topology_code_sync.cheng", 0},
    {"examples/unimaker_robot_node.cheng", 0},
    {"bootstrap/cheng_v2c.c", 0},
    {"bootstrap/cheng_v2_native_provider.c", 0},
    {"bootstrap/cheng_v2c_tooling.c", 0},
    {"bootstrap/cheng_v2c_tooling.h", 0},
    {"src/bootstrap/selfhost_contract.cheng", 0},
    {"src/bootstrap/tooling_selfhost_contract.cheng", 0},
    {"src/bootstrap/tooling_stage1_bootstrap.cheng", 0},
    {"src/bootstrap/unimaker_stage1_bootstrap.cheng", 0},
    {"src/compiler/driver/identity_v2.cheng", 0},
    {"src/compiler/driver/compiler_core_pipeline_v2.cheng", 0},
    {"src/compiler/driver/manifest_resolver_v2.cheng", 0},
    {"src/compiler/driver/lsmr_v2.cheng", 0},
    {"src/compiler/driver/network_distribution_pipeline_v2.cheng", 0},
    {"src/compiler/driver/network_distribution_identity_v2.cheng", 0},
    {"src/compiler/driver/release_artifact_v2.cheng", 0},
    {"src/compiler/driver/runtime_provider_object_v2.cheng", 0},
    {"src/compiler/driver/system_link_exec_v2.cheng", 0},
    {"src/compiler/driver/system_link_plan_v2.cheng", 0},
    {"src/compiler/frontend/compiler_core_surface_contract.cheng", 0},
    {"src/compiler/frontend/compiler_core_surface_ir_v2.cheng", 0},
    {"src/compiler/frontend/frontend_contract.cheng", 0},
    {"src/compiler/frontend/network_distribution_surface_contract.cheng", 0},
    {"src/compiler/frontend/network_distribution_surface_ir_v2.cheng", 0},
    {"src/compiler/frontend/string_surface_contract.cheng", 0},
    {"src/compiler/frontend/string_surface_ir_v2.cheng", 0},
    {"src/compiler/frontend/string_lowering_contract.cheng", 0},
    {"src/compiler/frontend/string_surface_lowering_v2.cheng", 0},
    {"src/compiler/frontend/unimaker_surface_contract.cheng", 0},
    {"src/compiler/frontend/unimaker_surface_ir_v2.cheng", 0},
    {"src/compiler/frontend/unimaker_surface_lowering_v2.cheng", 0},
    {"src/compiler/frontend/v2_source_parser.cheng", 0},
    {"src/runtime/compiler_core_runtime_v2.cheng", 0},
    {"src/runtime/compiler_core_native_dispatch.c", 0},
    {"src/runtime/compiler_core_native_dispatch.h", 0},
    {"src/runtime/compiler_core_native_provider.c", 0},
    {"src/runtime/core_runtime_v2.cheng", 0},
    {"src/runtime/lsmr_contract_v2.cheng", 0},
    {"src/runtime/network_distribution_runtime_v2.cheng", 0},
    {"src/runtime/strings_v2.cheng", 0},
    {"src/runtime/unimaker_runtime_contract_v2.cheng", 0},
    {"src/compiler/semantic_facts/facts_contract.cheng", 0},
    {"src/compiler/semantic_facts/compiler_core_facts_v2.cheng", 0},
    {"src/compiler/semantic_facts/network_distribution_facts_v2.cheng", 0},
    {"src/compiler/semantic_facts/string_facts_v2.cheng", 0},
    {"src/compiler/semantic_facts/unimaker_facts_v2.cheng", 0},
    {"src/compiler/high_uir/graph_contract.cheng", 0},
    {"src/compiler/high_uir/compiler_core_graph_v2.cheng", 0},
    {"src/compiler/high_uir/network_distribution_graph_v2.cheng", 0},
    {"src/compiler/high_uir/string_graph_v2.cheng", 0},
    {"src/compiler/high_uir/unimaker_graph_v2.cheng", 0},
    {"src/compiler/abi_bridge/bridge_contract.cheng", 0},
    {"src/compiler/low_uir/network_distribution_ops_contract.cheng", 0},
    {"src/compiler/low_uir/network_distribution_lowering_v2.cheng", 0},
    {"src/compiler/low_uir/pipeline_contract.cheng", 0},
    {"src/compiler/low_uir/compiler_core_ops_contract.cheng", 0},
    {"src/compiler/low_uir/compiler_core_lowering_v2.cheng", 0},
    {"src/compiler/low_uir/string_ops_contract.cheng", 0},
    {"src/compiler/low_uir/string_lowering_v2.cheng", 0},
    {"src/compiler/low_uir/unimaker_ops_contract.cheng", 0},
    {"src/compiler/low_uir/unimaker_lowering_v2.cheng", 0},
    {"src/compiler/machine/target_contract.cheng", 0},
    {"src/compiler/machine/machine_pipeline_v2.cheng", 0},
    {"src/compiler/obj/writer_contract.cheng", 0},
    {"src/compiler/obj/obj_image_v2.cheng", 0},
    {"src/compiler/obj/obj_file_v2.cheng", 0},
    {"src/compiler/driver/surface_contract.cheng", 0},
    {"src/compiler/driver/string_pipeline_v2.cheng", 0},
    {"src/compiler/driver/unimaker_pipeline_v2.cheng", 0},
    {"src/tooling/cheng_v2.cheng", 0},
    {"src/tooling/cheng_tooling_v2.cheng", 0},
    {"tests/contracts/soa_surface.cheng", 0},
    {"tests/contracts/pipeline_surface.cheng", 0},
    {"tests/contracts/string_runtime_surface.cheng", 0},
    {"tests/contracts/string_lowering_surface.cheng", 0},
    {"tests/contracts/string_pipeline_surface.cheng", 0},
    {"tests/contracts/string_pipeline_soa_surface.cheng", 0},
    {"tests/contracts/network_distribution_surface.cheng", 0},
    {"tests/contracts/network_distribution_pipeline_surface.cheng", 0},
    {"tests/contracts/compiler_core_surface.cheng", 0},
    {"tests/contracts/compiler_core_pipeline_surface.cheng", 0},
    {"tests/contracts/compiler_core_release_artifact.expected", 0},
    {"tests/contracts/compiler_core_system_link_plan.expected", 0},
    {"tests/contracts/compiler_core_system_link_exec.expected", 0},
    {"tests/contracts/compiler_core_system_link_exec_smoke.expected", 0},
    {"tests/contracts/full_selfhost.expected", 0},
    {"tests/contracts/lsmr_surface.cheng", 0},
    {"tests/contracts/lsmr_address.expected", 0},
    {"tests/contracts/lsmr_route_plan.expected", 0},
    {"tests/contracts/machine_pipeline_surface.cheng", 0},
    {"tests/contracts/obj_image_surface.cheng", 0},
    {"tests/contracts/obj_file_surface.cheng", 0},
    {"tests/contracts/runtime_provider_object_surface.cheng", 0},
    {"tests/contracts/system_link_exec_surface.cheng", 0},
    {"tests/contracts/system_link_plan_surface.cheng", 0},
    {"tests/contracts/obj_file_layout.expected", 0},
    {"tests/contracts/source_manifest_surface.cheng", 0},
    {"tests/contracts/release_artifact_surface.cheng", 0},
    {"tests/contracts/unimaker_surface.cheng", 0},
    {"tests/contracts/unimaker_pipeline_surface.cheng", 0},
    {"tests/contracts/network_selfhost.expected", 0},
    {"tests/contracts/selfhost_surface.cheng", 0},
    {"tests/contracts/selfhost_stage1_exec.expected", 0},
    {"tests/contracts/tooling_compiler_rule_pack.expected", 0},
    {"tests/contracts/tooling_release_artifact.expected", 0},
    {"tests/contracts/tooling_rule_pack.expected", 0},
    {"tests/contracts/tooling_selfhost.expected", 0},
    {"tests/contracts/tooling_selfhost_surface.cheng", 0},
    {"tests/contracts/tooling_source_manifest.expected", 0},
    {"tests/contracts/tooling_stage1_bootstrap_surface.cheng", 0},
    {"tests/contracts/unimaker_stage1_bootstrap_surface.cheng", 0},
    {"bootstrap/manifest.toml", 0},
};

static void usage(void) {
    puts("cheng_v2_bootstrap");
    puts("usage: cheng_v2_bootstrap <command> [root]");
    puts("");
    puts("commands:");
    puts("  help        show this message");
    puts("  check-tree  verify the required v2 tree exists");
    puts("  manifest    print a deterministic bootstrap manifest");
    puts("  contracts   print the required bootstrap-tracked files");
}

static int path_join(char *out, size_t out_cap, const char *root, const char *rel) {
    int n = snprintf(out, out_cap, "%s/%s", root, rel);
    if (n < 0 || (size_t)n >= out_cap) {
        return 0;
    }
    return 1;
}

static int stat_path(const char *path, struct stat *st) {
    if (stat(path, st) != 0) {
        return 0;
    }
    return 1;
}

static int check_one_path(const char *root, const RequiredPath *req) {
    char full[4096];
    struct stat st;
    if (!path_join(full, sizeof(full), root, req->path)) {
        fprintf(stderr, "[cheng_v2_bootstrap] path too long: %s/%s\n", root, req->path);
        return 0;
    }
    if (!stat_path(full, &st)) {
        fprintf(stderr, "[cheng_v2_bootstrap] missing path: %s\n", full);
        return 0;
    }
    if (req->is_dir) {
        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "[cheng_v2_bootstrap] expected directory: %s\n", full);
            return 0;
        }
    } else {
        if (!S_ISREG(st.st_mode)) {
            fprintf(stderr, "[cheng_v2_bootstrap] expected regular file: %s\n", full);
            return 0;
        }
    }
    return 1;
}

static uint64_t fnv1a_update(uint64_t h, const unsigned char *buf, size_t len) {
    size_t i;
    for (i = 0; i < len; ++i) {
        h ^= (uint64_t)buf[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t fnv1a_text(uint64_t h, const char *text) {
    return fnv1a_update(h, (const unsigned char *)text, strlen(text));
}

static uint64_t hash_file_into(uint64_t h, const char *path) {
    FILE *f;
    unsigned char buf[4096];
    size_t n;
    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "[cheng_v2_bootstrap] fopen(%s) failed: %s\n", path, strerror(errno));
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        h = fnv1a_update(h, buf, n);
    }
    if (ferror(f)) {
        fprintf(stderr, "[cheng_v2_bootstrap] fread(%s) failed\n", path);
        fclose(f);
        return 0;
    }
    fclose(f);
    return h;
}

static int cmd_check_tree(const char *root) {
    size_t i;
    for (i = 0; i < sizeof(k_required_paths) / sizeof(k_required_paths[0]); ++i) {
        if (!check_one_path(root, &k_required_paths[i])) {
            return 1;
        }
    }
    printf("status=ok\n");
    printf("root=%s\n", root);
    printf("required_paths=%zu\n", sizeof(k_required_paths) / sizeof(k_required_paths[0]));
    return 0;
}

static int cmd_contracts(void) {
    size_t i;
    for (i = 0; i < sizeof(k_required_paths) / sizeof(k_required_paths[0]); ++i) {
        puts(k_required_paths[i].path);
    }
    return 0;
}

static int cmd_manifest(const char *root) {
    size_t i;
    uint64_t h = 1469598103934665603ULL;
    char full[4096];
    if (cmd_check_tree(root) != 0) {
        return 1;
    }
    h = fnv1a_text(h, "v2.bootstrap.host.v1");
    for (i = 0; i < sizeof(k_required_paths) / sizeof(k_required_paths[0]); ++i) {
        if (!path_join(full, sizeof(full), root, k_required_paths[i].path)) {
            fprintf(stderr, "[cheng_v2_bootstrap] path too long: %s/%s\n", root, k_required_paths[i].path);
            return 1;
        }
        h = fnv1a_text(h, k_required_paths[i].path);
        h = hash_file_into(h, full);
        if (h == 0) {
            return 1;
        }
    }
    printf("bootstrap_version=v2.bootstrap.host.v1\n");
    printf("root=%s\n", root);
    printf("required_paths=%zu\n", sizeof(k_required_paths) / sizeof(k_required_paths[0]));
    printf("manifest_fnv1a64=%016llx\n", (unsigned long long)h);
    return 0;
}

int main(int argc, char **argv) {
    const char *cmd = "help";
    const char *root = ".";
    if (argc >= 2) {
        cmd = argv[1];
    }
    if (argc >= 3) {
        root = argv[2];
    }
    if (strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "help") == 0) {
        usage();
        return 0;
    }
    if (strcmp(cmd, "check-tree") == 0) {
        return cmd_check_tree(root);
    }
    if (strcmp(cmd, "contracts") == 0) {
        return cmd_contracts();
    }
    if (strcmp(cmd, "manifest") == 0) {
        return cmd_manifest(root);
    }
    usage();
    return 1;
}
