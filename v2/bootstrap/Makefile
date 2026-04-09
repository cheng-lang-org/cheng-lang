CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

OUT_DIR := ../artifacts/bootstrap
BOOTSTRAP_OUT := $(OUT_DIR)/cheng_v2_bootstrap
COMPILER_OUT := $(OUT_DIR)/cheng_v2c
ROOT_DIR := ..
SELFHOST_SOURCE := $(ROOT_DIR)/examples/unimaker_robot_node.cheng
SELFHOST_STAGE1 := $(OUT_DIR)/unimaker_stage1_bootstrap.generated.cheng
SELFHOST_STAGE1_C := $(OUT_DIR)/unimaker_stage1_bootstrap.c
SELFHOST_STAGE1_EXEC := $(OUT_DIR)/unimaker_stage1_bootstrap_exec
SELFHOST_STAGE1_OUT := $(OUT_DIR)/unimaker_stage1_bootstrap.out
SELFHOST_STAGE1_EXPECTED := $(ROOT_DIR)/tests/contracts/selfhost_stage1_exec.expected
SELFHOST_SYSTEM_LINK_PLAN_SHARED_STDOUT := $(OUT_DIR)/selfhost_system_link_plan_shared.stdout
SELFHOST_SYSTEM_LINK_PLAN_SHARED_FILE := $(OUT_DIR)/selfhost_system_link_plan_shared.txt
SELFHOST_SYSTEM_LINK_PLAN_SHARED_EXPECTED := $(ROOT_DIR)/tests/contracts/selfhost_system_link_plan_shared.expected
SELFHOST_SYSTEM_LINK_EXEC_SHARED_STDOUT := $(OUT_DIR)/selfhost_system_link_exec_shared.stdout
SELFHOST_SYSTEM_LINK_EXEC_SHARED_FILE := $(OUT_DIR)/selfhost_system_link_exec_shared.txt
SELFHOST_SYSTEM_LINK_EXEC_SHARED_BINARY := $(OUT_DIR)/selfhost_system_link_exec_shared.dylib
SELFHOST_SYSTEM_LINK_EXEC_SHARED_EXPECTED := $(ROOT_DIR)/tests/contracts/selfhost_system_link_exec_shared.expected
TOOLING_ROOT := $(ROOT_DIR)/examples
TOOLING_SOURCE := $(ROOT_DIR)/examples/network_distribution_module.cheng
TOPOLOGY_SOURCE := $(ROOT_DIR)/examples/topology_code_sync.cheng
TOOLING_STAGE1 := $(OUT_DIR)/tooling_stage1_bootstrap.generated.cheng
TOOLING_SOURCE_MANIFEST_STDOUT := $(OUT_DIR)/tooling_source_manifest.stdout
TOOLING_SOURCE_MANIFEST_FILE := $(OUT_DIR)/tooling_source_manifest.txt
TOOLING_SOURCE_MANIFEST_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_source_manifest.expected
TOOLING_RULE_PACK_STDOUT := $(OUT_DIR)/tooling_rule_pack.stdout
TOOLING_RULE_PACK_FILE := $(OUT_DIR)/tooling_rule_pack.txt
TOOLING_RULE_PACK_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_rule_pack.expected
TOOLING_COMPILER_RULE_PACK_STDOUT := $(OUT_DIR)/tooling_compiler_rule_pack.stdout
TOOLING_COMPILER_RULE_PACK_FILE := $(OUT_DIR)/tooling_compiler_rule_pack.txt
TOOLING_COMPILER_RULE_PACK_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_compiler_rule_pack.expected
TOOLING_RELEASE_STDOUT := $(OUT_DIR)/tooling_release_artifact.stdout
TOOLING_RELEASE_FILE := $(OUT_DIR)/tooling_release_artifact.txt
TOOLING_RELEASE_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_release_artifact.expected
TOOLING_SYSTEM_LINK_PLAN_SHARED_STDOUT := $(OUT_DIR)/tooling_system_link_plan_shared.stdout
TOOLING_SYSTEM_LINK_PLAN_SHARED_FILE := $(OUT_DIR)/tooling_system_link_plan_shared.txt
TOOLING_SYSTEM_LINK_PLAN_SHARED_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_system_link_plan_shared.expected
TOOLING_SYSTEM_LINK_EXEC_SHARED_STDOUT := $(OUT_DIR)/tooling_system_link_exec_shared.stdout
TOOLING_SYSTEM_LINK_EXEC_SHARED_FILE := $(OUT_DIR)/tooling_system_link_exec_shared.txt
TOOLING_SYSTEM_LINK_EXEC_SHARED_BINARY := $(OUT_DIR)/tooling_system_link_exec_shared.dylib
TOOLING_SYSTEM_LINK_EXEC_SHARED_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_system_link_exec_shared.expected
TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_STDOUT := $(OUT_DIR)/topology_system_link_plan_shared.stdout
TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_FILE := $(OUT_DIR)/topology_system_link_plan_shared.txt
TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_EXPECTED := $(ROOT_DIR)/tests/contracts/topology_system_link_plan_shared.expected
TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_STDOUT := $(OUT_DIR)/topology_system_link_exec_shared.stdout
TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_FILE := $(OUT_DIR)/topology_system_link_exec_shared.txt
TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_BINARY := $(OUT_DIR)/topology_system_link_exec_shared.dylib
TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_EXPECTED := $(ROOT_DIR)/tests/contracts/topology_system_link_exec_shared.expected
LSMR_ADDRESS_CID := 499128e86e4bf16650ba638299a0f91638a6c5edcc0f3e2674d4785b89632900
LSMR_DEPTH := 4
LSMR_ADDRESS_STDOUT := $(OUT_DIR)/lsmr_address.stdout
LSMR_ADDRESS_EXPECTED := $(ROOT_DIR)/tests/contracts/lsmr_address.expected
LSMR_ROUTE_PLAN_STDOUT := $(OUT_DIR)/lsmr_route_plan.stdout
LSMR_ROUTE_PLAN_EXPECTED := $(ROOT_DIR)/tests/contracts/lsmr_route_plan.expected
LSMR_ADVANCED_SMOKE_BINARY := $(OUT_DIR)/lsmr_advanced_features_smoke_bin
LSMR_ADVANCED_SMOKE_STDOUT := $(OUT_DIR)/lsmr_advanced_features_smoke.stdout
LSMR_ADVANCED_SMOKE_EXPECTED := $(ROOT_DIR)/tests/contracts/lsmr_advanced_features_smoke.expected
LSMR_STATE_TREE_SYNC_SMOKE_BINARY := $(OUT_DIR)/chain_state_tree_sync_smoke_test
LSMR_STATE_TREE_SYNC_SMOKE_STDOUT := $(OUT_DIR)/chain_state_tree_sync_smoke.stdout
LSMR_STATE_TREE_SYNC_SMOKE_EXPECTED := $(ROOT_DIR)/tests/contracts/chain_state_tree_sync_smoke.expected
LSMR_CHAIN_SMOKE_BINARY := $(OUT_DIR)/msquic_chain_smoke_test
LSMR_CHAIN_SMOKE_STDOUT := $(OUT_DIR)/msquic_chain_smoke.stdout
LSMR_CHAIN_SMOKE_EXPECTED := $(ROOT_DIR)/tests/contracts/msquic_chain_smoke.expected
LSMR_CHAIN_NODE_BINARY := $(OUT_DIR)/chain_node_test
LSMR_CHAIN_NODE_PROCESS_SMOKE_STDOUT := $(OUT_DIR)/chain_node_process_smoke.stdout
LSMR_CHAIN_NODE_PROCESS_SMOKE_EXPECTED := $(ROOT_DIR)/tests/contracts/chain_node_process_smoke.expected
OBJ_FILE_STDOUT := $(OUT_DIR)/obj_file_layout.stdout
OBJ_FILE_EXPECTED := $(ROOT_DIR)/tests/contracts/obj_file_layout.expected
COMPILER_CORE_SOURCE := $(ROOT_DIR)/src/tooling/cheng_v2.cheng
COMPILER_CORE_RELEASE_STDOUT := $(OUT_DIR)/compiler_core_release_artifact.stdout
COMPILER_CORE_RELEASE_FILE := $(OUT_DIR)/compiler_core_release_artifact.txt
COMPILER_CORE_RELEASE_EXPECTED := $(ROOT_DIR)/tests/contracts/compiler_core_release_artifact.expected
COMPILER_CORE_SYSTEM_LINK_STDOUT := $(OUT_DIR)/compiler_core_system_link_plan.stdout
COMPILER_CORE_SYSTEM_LINK_FILE := $(OUT_DIR)/compiler_core_system_link_plan.txt
COMPILER_CORE_SYSTEM_LINK_EXPECTED := $(ROOT_DIR)/tests/contracts/compiler_core_system_link_plan.expected
COMPILER_CORE_SYSTEM_LINK_EXEC_STDOUT := $(OUT_DIR)/compiler_core_system_link_exec.stdout
COMPILER_CORE_SYSTEM_LINK_EXEC_FILE := $(OUT_DIR)/compiler_core_system_link_exec.txt
COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY := $(OUT_DIR)/compiler_core_system_link_exec
COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS := $(OUT_DIR)/cheng_v2_system_link_exec
COMPILER_CORE_SYSTEM_LINK_EXEC_EXPECTED := $(ROOT_DIR)/tests/contracts/compiler_core_system_link_exec.expected
COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE := $(OUT_DIR)/compiler_core_system_link_exec.smoke
COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE_EXPECTED := $(ROOT_DIR)/tests/contracts/compiler_core_system_link_exec_smoke.expected
TAIJI_SEED_STAGE0_DIR := $(OUT_DIR)/taiji_seed_stage0
TAIJI_SEED_STAGE3_DIR := $(OUT_DIR)/taiji_seed_stage3
TAIJI_SEED_STAGE0_SHA := $(OUT_DIR)/taiji_seed_stage0.sha256
TAIJI_SEED_STAGE3_SHA := $(OUT_DIR)/taiji_seed_stage3.sha256
NETWORK_SELFHOST_OUT := $(OUT_DIR)/network_selfhost.out
NETWORK_SELFHOST_EXPECTED := $(ROOT_DIR)/tests/contracts/network_selfhost.expected
COMPILER_CORE_ZRPC_SURFACE_OUT := $(OUT_DIR)/compiler_core_zrpc_surface.out
COMPILER_CORE_ZRPC_SURFACE_EXPECTED := $(ROOT_DIR)/tests/contracts/compiler_core_zrpc_surface.expected
V2_NO_POINTER_SURFACE_OUT := $(OUT_DIR)/v2_no_pointer_surface.out
V2_NO_POINTER_SURFACE_EXPECTED := $(ROOT_DIR)/tests/contracts/v2_no_pointer_surface.expected
V2_NO_POINTER_COMPILE_FAILURES_OUT := $(OUT_DIR)/v2_no_pointer_compile_failures.out
V2_NO_POINTER_COMPILE_FAILURES_EXPECTED := $(ROOT_DIR)/tests/contracts/v2_no_pointer_compile_failures.expected
V2_NO_POINTER_PACKAGE_COMPILE_CLOSURES_OUT := $(OUT_DIR)/v2_no_pointer_package_compile_closures.out
V2_NO_POINTER_PACKAGE_COMPILE_CLOSURES_EXPECTED := $(ROOT_DIR)/tests/contracts/v2_no_pointer_package_compile_closures.expected
V2_NO_POINTER_PACKAGE_CLOSURES_OUT := $(OUT_DIR)/v2_no_pointer_package_closures.out
V2_NO_POINTER_PACKAGE_CLOSURES_EXPECTED := $(ROOT_DIR)/tests/contracts/v2_no_pointer_package_closures.expected
COMPILER_CORE_NO_POINTER_CLOSURE_OUT := $(OUT_DIR)/compiler_core_no_pointer_closure.out
COMPILER_CORE_NO_POINTER_CLOSURE_EXPECTED := $(ROOT_DIR)/tests/contracts/compiler_core_no_pointer_closure.expected
COMPILER_CORE_CYCLE_SURFACE_OUT := $(OUT_DIR)/compiler_core_cycle_surface.out
COMPILER_CORE_CYCLE_SURFACE_EXPECTED := $(ROOT_DIR)/tests/contracts/compiler_core_cycle_surface.expected
TOOLING_SELFHOST_OUT := $(OUT_DIR)/tooling_selfhost.out
TOOLING_SELFHOST_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_selfhost.expected
PROGRAM_SELFHOST_STDOUT := $(OUT_DIR)/program_selfhost.stdout
PROGRAM_SELFHOST_EXPECTED := $(ROOT_DIR)/tests/contracts/program_selfhost.expected
FULL_SELFHOST_STDOUT := $(OUT_DIR)/full_selfhost.stdout
FULL_SELFHOST_EXPECTED := $(ROOT_DIR)/tests/contracts/full_selfhost.expected

.PHONY: all clean selfhost tooling-selfhost program-selfhost compiler-core-release compiler-core-system-link compiler-core-system-link-exec obj-file-layout lsmr-contracts full-selfhost

all: $(BOOTSTRAP_OUT) $(COMPILER_OUT)

$(BOOTSTRAP_OUT): cheng_v2_bootstrap.c
	mkdir -p "$(OUT_DIR)"
	$(CC) $(CFLAGS) cheng_v2_bootstrap.c -o "$(BOOTSTRAP_OUT)"

$(COMPILER_OUT): cheng_v2c.c cheng_v2c_tooling.c cheng_v2c_tooling.h
	mkdir -p "$(OUT_DIR)"
	$(CC) $(CFLAGS) cheng_v2c.c cheng_v2c_tooling.c -o "$(COMPILER_OUT)"

selfhost: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" selfhost-check "$(SELFHOST_SOURCE)"
	"$(COMPILER_OUT)" emit-stage1 "$(SELFHOST_SOURCE)" "$(SELFHOST_STAGE1)"
	cmp -s "$(ROOT_DIR)/src/bootstrap/unimaker_stage1_bootstrap.cheng" "$(SELFHOST_STAGE1)" || { echo "selfhost mismatch: checked-in stage1 source" 1>&2; exit 1; }
	"$(COMPILER_OUT)" emit-c "$(ROOT_DIR)/src/bootstrap/unimaker_stage1_bootstrap.cheng" "$(SELFHOST_STAGE1_C)"
	$(CC) $(CFLAGS) "$(SELFHOST_STAGE1_C)" -o "$(SELFHOST_STAGE1_EXEC)"
	"$(SELFHOST_STAGE1_EXEC)" > "$(SELFHOST_STAGE1_OUT)"
	cmp -s "$(SELFHOST_STAGE1_EXPECTED)" "$(SELFHOST_STAGE1_OUT)" || { echo "selfhost mismatch: checked-in stage1 exec output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-plan --in "$(SELFHOST_SOURCE)" --emit shared --target arm64-apple-darwin --out "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_FILE)" > "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_STDOUT)"
	cmp -s "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_EXPECTED)" "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_STDOUT)" || { echo "selfhost mismatch: shared system-link-plan stdout" 1>&2; exit 1; }
	cmp -s "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_EXPECTED)" "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_FILE)" || { echo "selfhost mismatch: shared system-link-plan file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-exec --in "$(SELFHOST_SOURCE)" --emit shared --target arm64-apple-darwin --out "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_BINARY)" --report-out "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_FILE)" > "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_STDOUT)"
	cmp -s "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_EXPECTED)" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_STDOUT)" || { echo "selfhost mismatch: shared system-link-exec stdout" 1>&2; exit 1; }
	cmp -s "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_EXPECTED)" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_FILE)" || { echo "selfhost mismatch: shared system-link-exec file" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

tooling-selfhost: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" resolve-manifest --root "$(TOOLING_ROOT)" > "$(TOOLING_SOURCE_MANIFEST_STDOUT)"
	cmp -s "$(TOOLING_SOURCE_MANIFEST_EXPECTED)" "$(TOOLING_SOURCE_MANIFEST_STDOUT)" || { echo "tooling-selfhost mismatch: resolve-manifest output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" publish-source-manifest --root "$(TOOLING_ROOT)" --out "$(TOOLING_SOURCE_MANIFEST_FILE)" > "$(TOOLING_SOURCE_MANIFEST_STDOUT)"
	cmp -s "$(TOOLING_SOURCE_MANIFEST_EXPECTED)" "$(TOOLING_SOURCE_MANIFEST_STDOUT)" || { echo "tooling-selfhost mismatch: publish-source-manifest stdout" 1>&2; exit 1; }
	cmp -s "$(TOOLING_SOURCE_MANIFEST_EXPECTED)" "$(TOOLING_SOURCE_MANIFEST_FILE)" || { echo "tooling-selfhost mismatch: publish-source-manifest file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" publish-rule-pack --in "$(TOOLING_SOURCE)" --out "$(TOOLING_RULE_PACK_FILE)" > "$(TOOLING_RULE_PACK_STDOUT)"
	cmp -s "$(TOOLING_RULE_PACK_EXPECTED)" "$(TOOLING_RULE_PACK_STDOUT)" || { echo "tooling-selfhost mismatch: publish-rule-pack stdout" 1>&2; exit 1; }
	cmp -s "$(TOOLING_RULE_PACK_EXPECTED)" "$(TOOLING_RULE_PACK_FILE)" || { echo "tooling-selfhost mismatch: publish-rule-pack file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" publish-compiler-rule-pack --in "$(TOOLING_SOURCE)" --out "$(TOOLING_COMPILER_RULE_PACK_FILE)" > "$(TOOLING_COMPILER_RULE_PACK_STDOUT)"
	cmp -s "$(TOOLING_COMPILER_RULE_PACK_EXPECTED)" "$(TOOLING_COMPILER_RULE_PACK_STDOUT)" || { echo "tooling-selfhost mismatch: publish-compiler-rule-pack stdout" 1>&2; exit 1; }
	cmp -s "$(TOOLING_COMPILER_RULE_PACK_EXPECTED)" "$(TOOLING_COMPILER_RULE_PACK_FILE)" || { echo "tooling-selfhost mismatch: publish-compiler-rule-pack file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" release-compile --in "$(TOOLING_SOURCE)" --emit exe --target arm64-apple-darwin --out "$(TOOLING_RELEASE_FILE)" > "$(TOOLING_RELEASE_STDOUT)"
	cmp -s "$(TOOLING_RELEASE_EXPECTED)" "$(TOOLING_RELEASE_STDOUT)" || { echo "tooling-selfhost mismatch: release-compile stdout" 1>&2; exit 1; }
	cmp -s "$(TOOLING_RELEASE_EXPECTED)" "$(TOOLING_RELEASE_FILE)" || { echo "tooling-selfhost mismatch: release-compile file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-plan --in "$(TOOLING_SOURCE)" --emit shared --target arm64-apple-darwin --out "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_FILE)" > "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_STDOUT)"
	cmp -s "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_EXPECTED)" "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_STDOUT)" || { echo "tooling-selfhost mismatch: shared system-link-plan stdout" 1>&2; exit 1; }
	cmp -s "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_EXPECTED)" "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_FILE)" || { echo "tooling-selfhost mismatch: shared system-link-plan file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-exec --in "$(TOOLING_SOURCE)" --emit shared --target arm64-apple-darwin --out "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_BINARY)" --report-out "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_FILE)" > "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_STDOUT)"
	cmp -s "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_EXPECTED)" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_STDOUT)" || { echo "tooling-selfhost mismatch: shared system-link-exec stdout" 1>&2; exit 1; }
	cmp -s "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_EXPECTED)" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_FILE)" || { echo "tooling-selfhost mismatch: shared system-link-exec file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-plan --in "$(TOPOLOGY_SOURCE)" --emit shared --target arm64-apple-darwin --out "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_FILE)" > "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_STDOUT)"
	cmp -s "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_EXPECTED)" "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_STDOUT)" || { echo "tooling-selfhost mismatch: topology shared system-link-plan stdout" 1>&2; exit 1; }
	cmp -s "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_EXPECTED)" "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_FILE)" || { echo "tooling-selfhost mismatch: topology shared system-link-plan file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-exec --in "$(TOPOLOGY_SOURCE)" --emit shared --target arm64-apple-darwin --out "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_BINARY)" --report-out "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_FILE)" > "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_STDOUT)"
	cmp -s "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_EXPECTED)" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_STDOUT)" || { echo "tooling-selfhost mismatch: topology shared system-link-exec stdout" 1>&2; exit 1; }
	cmp -s "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_EXPECTED)" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_FILE)" || { echo "tooling-selfhost mismatch: topology shared system-link-exec file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" verify-network-selfhost --root "$(TOOLING_ROOT)" --in "$(TOOLING_SOURCE)" > "$(NETWORK_SELFHOST_OUT)"
	cmp -s "$(NETWORK_SELFHOST_EXPECTED)" "$(NETWORK_SELFHOST_OUT)" || { echo "tooling-selfhost mismatch: verify-network-selfhost output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" emit-tooling-stage1 --root "$(TOOLING_ROOT)" --in "$(TOOLING_SOURCE)" --out "$(TOOLING_STAGE1)"
	cmp -s "$(ROOT_DIR)/src/bootstrap/tooling_stage1_bootstrap.cheng" "$(TOOLING_STAGE1)" || { echo "tooling-selfhost mismatch: checked-in tooling stage1 source" 1>&2; exit 1; }
	"$(COMPILER_OUT)" tooling-selfhost-check --root "$(TOOLING_ROOT)" --in "$(TOOLING_SOURCE)" > "$(TOOLING_SELFHOST_OUT)"
	cmp -s "$(TOOLING_SELFHOST_EXPECTED)" "$(TOOLING_SELFHOST_OUT)" || { echo "tooling-selfhost mismatch: tooling-selfhost-check output" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

lsmr-contracts: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" lsmr-address --cid "$(LSMR_ADDRESS_CID)" --depth "$(LSMR_DEPTH)" > "$(LSMR_ADDRESS_STDOUT)"
	cmp -s "$(LSMR_ADDRESS_EXPECTED)" "$(LSMR_ADDRESS_STDOUT)" || { echo "lsmr-contracts mismatch: lsmr-address output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" lsmr-route-plan --cid "$(LSMR_ADDRESS_CID)" --depth "$(LSMR_DEPTH)" --priority urgent --dispersal none > "$(LSMR_ROUTE_PLAN_STDOUT)"
	cmp -s "$(LSMR_ROUTE_PLAN_EXPECTED)" "$(LSMR_ROUTE_PLAN_STDOUT)" || { echo "lsmr-contracts mismatch: lsmr-route-plan output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-exec --in "$(ROOT_DIR)/cheng-quic/src/project/chain_node.cheng" --out "$(LSMR_CHAIN_NODE_BINARY)" --emit exe --target arm64-apple-darwin > /dev/null
	{ \
		port=$$((20000 + ($$$$ % 20000))); \
		listen="/ip4/127.0.0.1/udp/$$port/quic-v1"; \
		server_root="$(OUT_DIR)/chain_node_process_server_root"; \
		client_root="$(OUT_DIR)/chain_node_process_client_root"; \
		server_log="$(OUT_DIR)/chain_node_process_server.stdout"; \
		sync_log="$(OUT_DIR)/chain_node_process_client_sync.stdout"; \
		rm -rf "$$server_root" "$$client_root" "$$server_log" "$$sync_log"; \
		"$(LSMR_CHAIN_NODE_BINARY)" mint --root "$$server_root" --node-id server --account alice --asset cheng --amount 11 > /dev/null || exit 1; \
		printf "chain_node_process_smoke_stage=mint_ok\n"; \
		(timeout 60s "$(LSMR_CHAIN_NODE_BINARY)" serve-once --root "$$server_root" --node-id server --listen "$$listen" > "$$server_log") & \
		server_pid=$$!; \
		sleep 1; \
		timeout 90s "$(LSMR_CHAIN_NODE_BINARY)" sync-once --root "$$client_root" --node-id client --peer "$$listen" > "$$sync_log" || { wait $$server_pid || true; exit 1; }; \
		sync_out=$$(tail -n 1 "$$sync_log"); \
		printf "chain_node_process_smoke_stage=sync_ok\n"; \
		balance_out=$$(timeout 10s "$(LSMR_CHAIN_NODE_BINARY)" balance --root "$$client_root" --node-id client --account alice --asset cheng) || { wait $$server_pid || true; exit 1; }; \
		printf "chain_node_process_smoke_stage=balance_ok\n"; \
		wait $$server_pid || exit 1; \
		server_out=$$(tail -n 1 "$$server_log"); \
		printf "sync_output=%s\n" "$$sync_out"; \
		printf "balance_output=%s\n" "$$balance_out"; \
		printf "server_output=%s\n" "$$server_out"; \
		printf "chain_node_process_smoke=ok\n"; \
	} > "$(LSMR_CHAIN_NODE_PROCESS_SMOKE_STDOUT)"
	cmp -s "$(LSMR_CHAIN_NODE_PROCESS_SMOKE_EXPECTED)" "$(LSMR_CHAIN_NODE_PROCESS_SMOKE_STDOUT)" || { echo "lsmr-contracts mismatch: chain node process smoke output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-exec --in "$(ROOT_DIR)/cheng-quic/src/tests/lsmr_advanced_features_smoke.cheng" --out "$(LSMR_ADVANCED_SMOKE_BINARY)" --emit exe --target arm64-apple-darwin > /dev/null
	"$(LSMR_ADVANCED_SMOKE_BINARY)" > "$(LSMR_ADVANCED_SMOKE_STDOUT)"
	cmp -s "$(LSMR_ADVANCED_SMOKE_EXPECTED)" "$(LSMR_ADVANCED_SMOKE_STDOUT)" || { echo "lsmr-contracts mismatch: lsmr advanced features smoke output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" system-link-exec --in "$(ROOT_DIR)/cheng-quic/src/tests/chain_state_tree_sync_smoke.cheng" --out "$(LSMR_STATE_TREE_SYNC_SMOKE_BINARY)" --emit exe --target arm64-apple-darwin > /dev/null
	timeout 30s "$(LSMR_STATE_TREE_SYNC_SMOKE_BINARY)" > "$(LSMR_STATE_TREE_SYNC_SMOKE_STDOUT)"
	cmp -s "$(LSMR_STATE_TREE_SYNC_SMOKE_EXPECTED)" "$(LSMR_STATE_TREE_SYNC_SMOKE_STDOUT)" || { echo "lsmr-contracts mismatch: chain state tree sync smoke output" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

compiler-core-release: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" release-compile --in "$(COMPILER_CORE_SOURCE)" --emit exe --target arm64-apple-darwin --out "$(COMPILER_CORE_RELEASE_FILE)" > "$(COMPILER_CORE_RELEASE_STDOUT)"
	cmp -s "$(COMPILER_CORE_RELEASE_EXPECTED)" "$(COMPILER_CORE_RELEASE_STDOUT)" || { echo "compiler-core-release mismatch: release-compile stdout" 1>&2; exit 1; }
	cmp -s "$(COMPILER_CORE_RELEASE_EXPECTED)" "$(COMPILER_CORE_RELEASE_FILE)" || { echo "compiler-core-release mismatch: release-compile file" 1>&2; exit 1; }
	"$(COMPILER_OUT)" verify-compiler-core-zrpc-surface --root "$(ROOT_DIR)" > "$(COMPILER_CORE_ZRPC_SURFACE_OUT)"
	cmp -s "$(COMPILER_CORE_ZRPC_SURFACE_EXPECTED)" "$(COMPILER_CORE_ZRPC_SURFACE_OUT)" || { echo "compiler-core-release mismatch: verify-compiler-core-zrpc-surface output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" verify-v2-no-pointer-surface --root "$(ROOT_DIR)" > "$(V2_NO_POINTER_SURFACE_OUT)"
	cmp -s "$(V2_NO_POINTER_SURFACE_EXPECTED)" "$(V2_NO_POINTER_SURFACE_OUT)" || { echo "compiler-core-release mismatch: verify-v2-no-pointer-surface output" 1>&2; exit 1; }
	{ \
		printf "case_count=3\n"; \
		printf "case.0.id=unimaker_fail\n"; \
		printf "case.0.path=v2/tests/contracts/v2_no_pointer_compile_failures_fixture/unimaker/unimaker_surface_ptr_probe.cheng\n"; \
		case0_output=$$("$(COMPILER_OUT)" release-compile --in "$(ROOT_DIR)/tests/contracts/v2_no_pointer_compile_failures_fixture/unimaker/unimaker_surface_ptr_probe.cheng" --emit exe --target arm64-apple-darwin 2>&1); \
		case0_rc=$$?; \
		printf "case.0.output=%s\n" "$$case0_output"; \
		printf "case.0.rc=%d\n" "$$case0_rc"; \
		printf "case.1.id=topology_fail\n"; \
		printf "case.1.path=v2/tests/contracts/v2_no_pointer_compile_failures_fixture/topology/topology_surface_ptr_probe.cheng\n"; \
		case1_output=$$("$(COMPILER_OUT)" release-compile --in "$(ROOT_DIR)/tests/contracts/v2_no_pointer_compile_failures_fixture/topology/topology_surface_ptr_probe.cheng" --emit exe --target arm64-apple-darwin 2>&1); \
		case1_rc=$$?; \
		printf "case.1.output=%s\n" "$$case1_output"; \
		printf "case.1.rc=%d\n" "$$case1_rc"; \
		printf "case.2.id=network_fail\n"; \
		printf "case.2.path=v2/tests/contracts/v2_no_pointer_compile_failures_fixture/network/network_surface_rawmem_probe.cheng\n"; \
		case2_output=$$("$(COMPILER_OUT)" release-compile --in "$(ROOT_DIR)/tests/contracts/v2_no_pointer_compile_failures_fixture/network/network_surface_rawmem_probe.cheng" --emit exe --target arm64-apple-darwin 2>&1); \
		case2_rc=$$?; \
		printf "case.2.output=%s\n" "$$case2_output"; \
		printf "case.2.rc=%d\n" "$$case2_rc"; \
		fail_count=0; \
		test "$$case0_rc" -eq 1 && fail_count=$$((fail_count + 1)); \
		test "$$case1_rc" -eq 1 && fail_count=$$((fail_count + 1)); \
		test "$$case2_rc" -eq 1 && fail_count=$$((fail_count + 1)); \
		printf "fail_count=%d\n" "$$fail_count"; \
		if test "$$fail_count" -eq 3; then \
			printf "v2_no_pointer_compile_failures_ok=1\n"; \
		else \
			printf "v2_no_pointer_compile_failures_ok=0\n"; \
		fi; \
	} > "$(V2_NO_POINTER_COMPILE_FAILURES_OUT)"
	cmp -s "$(V2_NO_POINTER_COMPILE_FAILURES_EXPECTED)" "$(V2_NO_POINTER_COMPILE_FAILURES_OUT)" || { echo "compiler-core-release mismatch: v2 no-pointer compile failures output" 1>&2; exit 1; }
	{ \
		printf "case_count=3\n"; \
		printf "case.0.id=unimaker_entry\n"; \
		printf "case.0.path=v2/examples/unimaker_robot_node.cheng\n"; \
		case0_output=$$("$(COMPILER_OUT)" release-compile --in "$(ROOT_DIR)/examples/unimaker_robot_node.cheng" --emit exe --target arm64-apple-darwin 2>&1); \
		case0_rc=$$?; \
		printf "case.0.rc=%d\n" "$$case0_rc"; \
		printf "case.0.module_kind=%s\n" "$$(printf "%s\n" "$$case0_output" | awk -F= '/^module_kind=/{print $$2; exit}')"; \
		printf "case.0.manifest_file_count=%s\n" "$$(printf "%s\n" "$$case0_output" | awk -F= '/^manifest_file_count=/{print $$2; exit}')"; \
		printf "case.0.no_pointer_checked_file_count=%s\n" "$$(printf "%s\n" "$$case0_output" | awk -F= '/^no_pointer_checked_file_count=/{print $$2; exit}')"; \
		printf "case.0.no_pointer_checked_compiler_core_surface_file_count=%s\n" "$$(printf "%s\n" "$$case0_output" | awk -F= '/^no_pointer_checked_compiler_core_surface_file_count=/{print $$2; exit}')"; \
		printf "case.0.no_pointer_checked_line_surface_file_count=%s\n" "$$(printf "%s\n" "$$case0_output" | awk -F= '/^no_pointer_checked_line_surface_file_count=/{print $$2; exit}')"; \
		printf "case.0.no_pointer_closure_ok=%s\n" "$$(printf "%s\n" "$$case0_output" | awk -F= '/^no_pointer_closure_ok=/{print $$2; exit}')"; \
		printf "case.1.id=network_entry\n"; \
		printf "case.1.path=v2/examples/network_distribution_module.cheng\n"; \
		case1_output=$$("$(COMPILER_OUT)" release-compile --in "$(ROOT_DIR)/examples/network_distribution_module.cheng" --emit exe --target arm64-apple-darwin 2>&1); \
		case1_rc=$$?; \
		printf "case.1.rc=%d\n" "$$case1_rc"; \
		printf "case.1.module_kind=%s\n" "$$(printf "%s\n" "$$case1_output" | awk -F= '/^module_kind=/{print $$2; exit}')"; \
		printf "case.1.manifest_file_count=%s\n" "$$(printf "%s\n" "$$case1_output" | awk -F= '/^manifest_file_count=/{print $$2; exit}')"; \
		printf "case.1.no_pointer_checked_file_count=%s\n" "$$(printf "%s\n" "$$case1_output" | awk -F= '/^no_pointer_checked_file_count=/{print $$2; exit}')"; \
		printf "case.1.no_pointer_checked_compiler_core_surface_file_count=%s\n" "$$(printf "%s\n" "$$case1_output" | awk -F= '/^no_pointer_checked_compiler_core_surface_file_count=/{print $$2; exit}')"; \
		printf "case.1.no_pointer_checked_line_surface_file_count=%s\n" "$$(printf "%s\n" "$$case1_output" | awk -F= '/^no_pointer_checked_line_surface_file_count=/{print $$2; exit}')"; \
		printf "case.1.no_pointer_closure_ok=%s\n" "$$(printf "%s\n" "$$case1_output" | awk -F= '/^no_pointer_closure_ok=/{print $$2; exit}')"; \
		printf "case.2.id=topology_entry\n"; \
		printf "case.2.path=v2/examples/topology_code_sync.cheng\n"; \
		case2_output=$$("$(COMPILER_OUT)" release-compile --in "$(ROOT_DIR)/examples/topology_code_sync.cheng" --emit exe --target arm64-apple-darwin 2>&1); \
		case2_rc=$$?; \
		printf "case.2.rc=%d\n" "$$case2_rc"; \
		printf "case.2.module_kind=%s\n" "$$(printf "%s\n" "$$case2_output" | awk -F= '/^module_kind=/{print $$2; exit}')"; \
		printf "case.2.manifest_file_count=%s\n" "$$(printf "%s\n" "$$case2_output" | awk -F= '/^manifest_file_count=/{print $$2; exit}')"; \
		printf "case.2.no_pointer_checked_file_count=%s\n" "$$(printf "%s\n" "$$case2_output" | awk -F= '/^no_pointer_checked_file_count=/{print $$2; exit}')"; \
		printf "case.2.no_pointer_checked_compiler_core_surface_file_count=%s\n" "$$(printf "%s\n" "$$case2_output" | awk -F= '/^no_pointer_checked_compiler_core_surface_file_count=/{print $$2; exit}')"; \
		printf "case.2.no_pointer_checked_line_surface_file_count=%s\n" "$$(printf "%s\n" "$$case2_output" | awk -F= '/^no_pointer_checked_line_surface_file_count=/{print $$2; exit}')"; \
		printf "case.2.no_pointer_closure_ok=%s\n" "$$(printf "%s\n" "$$case2_output" | awk -F= '/^no_pointer_closure_ok=/{print $$2; exit}')"; \
		pass_count=0; \
		test "$$case0_rc" -eq 0 && test "$$(printf "%s\n" "$$case0_output" | awk -F= '/^no_pointer_closure_ok=/{print $$2; exit}')" = "1" && pass_count=$$((pass_count + 1)); \
		test "$$case1_rc" -eq 0 && test "$$(printf "%s\n" "$$case1_output" | awk -F= '/^no_pointer_closure_ok=/{print $$2; exit}')" = "1" && pass_count=$$((pass_count + 1)); \
		test "$$case2_rc" -eq 0 && test "$$(printf "%s\n" "$$case2_output" | awk -F= '/^no_pointer_closure_ok=/{print $$2; exit}')" = "1" && pass_count=$$((pass_count + 1)); \
		printf "pass_count=%d\n" "$$pass_count"; \
		if test "$$pass_count" -eq 3; then \
			printf "v2_no_pointer_package_compile_closures_ok=1\n"; \
		else \
			printf "v2_no_pointer_package_compile_closures_ok=0\n"; \
		fi; \
	} > "$(V2_NO_POINTER_PACKAGE_COMPILE_CLOSURES_OUT)"
	cmp -s "$(V2_NO_POINTER_PACKAGE_COMPILE_CLOSURES_EXPECTED)" "$(V2_NO_POINTER_PACKAGE_COMPILE_CLOSURES_OUT)" || { echo "compiler-core-release mismatch: v2 no-pointer package compile closures output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" verify-v2-no-pointer-package-closures --root "$(ROOT_DIR)" > "$(V2_NO_POINTER_PACKAGE_CLOSURES_OUT)"
	cmp -s "$(V2_NO_POINTER_PACKAGE_CLOSURES_EXPECTED)" "$(V2_NO_POINTER_PACKAGE_CLOSURES_OUT)" || { echo "compiler-core-release mismatch: verify-v2-no-pointer-package-closures output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" verify-compiler-core-no-pointer-closure --root "$(ROOT_DIR)" > "$(COMPILER_CORE_NO_POINTER_CLOSURE_OUT)"
	cmp -s "$(COMPILER_CORE_NO_POINTER_CLOSURE_EXPECTED)" "$(COMPILER_CORE_NO_POINTER_CLOSURE_OUT)" || { echo "compiler-core-release mismatch: verify-compiler-core-no-pointer-closure output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" verify-compiler-core-cycle-surface --root "$(ROOT_DIR)" > "$(COMPILER_CORE_CYCLE_SURFACE_OUT)"
	cmp -s "$(COMPILER_CORE_CYCLE_SURFACE_EXPECTED)" "$(COMPILER_CORE_CYCLE_SURFACE_OUT)" || { echo "compiler-core-release mismatch: verify-compiler-core-cycle-surface output" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

compiler-core-system-link: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" system-link-plan --in "$(COMPILER_CORE_SOURCE)" --emit exe --target arm64-apple-darwin --out "$(COMPILER_CORE_SYSTEM_LINK_FILE)" > "$(COMPILER_CORE_SYSTEM_LINK_STDOUT)"
	cmp -s "$(COMPILER_CORE_SYSTEM_LINK_EXPECTED)" "$(COMPILER_CORE_SYSTEM_LINK_STDOUT)" || { echo "compiler-core-system-link mismatch: system-link-plan stdout" 1>&2; exit 1; }
	cmp -s "$(COMPILER_CORE_SYSTEM_LINK_EXPECTED)" "$(COMPILER_CORE_SYSTEM_LINK_FILE)" || { echo "compiler-core-system-link mismatch: system-link-plan file" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

compiler-core-system-link-exec: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" system-link-exec --in "$(COMPILER_CORE_SOURCE)" --emit exe --target arm64-apple-darwin --out "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" --report-out "$(COMPILER_CORE_SYSTEM_LINK_EXEC_FILE)" > "$(COMPILER_CORE_SYSTEM_LINK_EXEC_STDOUT)"
	cmp -s "$(COMPILER_CORE_SYSTEM_LINK_EXEC_EXPECTED)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_STDOUT)" || { echo "compiler-core-system-link-exec mismatch: system-link-exec stdout" 1>&2; exit 1; }
	cmp -s "$(COMPILER_CORE_SYSTEM_LINK_EXEC_EXPECTED)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_FILE)" || { echo "compiler-core-system-link-exec mismatch: system-link-exec file" 1>&2; exit 1; }
	rm -f "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS).tmp"
	cp "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS).tmp"
	mv -f "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS).tmp" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)"
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" status > "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" 2>&1; rc=$$?; printf "status_rc=%d\n" "$$rc" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)"; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" release-compile --in "$(TOOLING_SOURCE)" --emit exe --target arm64-apple-darwin >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" 2>&1; rc=$$?; printf "release_compile_rc=%d\n" "$$rc" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)"; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" lsmr-address --cid "$(LSMR_ADDRESS_CID)" --depth "$(LSMR_DEPTH)" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" 2>&1; rc=$$?; printf "lsmr_address_rc=%d\n" "$$rc" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)"; test "$$rc" -eq 0; }
	cmp -s "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE_EXPECTED)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" || { echo "compiler-core-system-link-exec mismatch: native launch smoke" 1>&2; exit 1; }
	! rg -q "compiler_core_replay_ok|runtime/compiler_core.command_dispatch|native_tooling_dispatch|external_cc_obj" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" || { echo "compiler-core-system-link-exec mismatch: legacy compiler_core dispatch text present" 1>&2; exit 1; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" trace-phase-space > /dev/null 2>&1; rc=$$?; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" emit-proof-obligations > /dev/null 2>&1; rc=$$?; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" taiji-seed-set > /dev/null 2>&1; rc=$$?; test "$$rc" -eq 0; }
	rm -rf "$(TAIJI_SEED_STAGE0_DIR)" "$(TAIJI_SEED_STAGE3_DIR)"
	"$(COMPILER_OUT)" taiji-seed-set --out-dir "$(TAIJI_SEED_STAGE0_DIR)" > /dev/null
	"$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" taiji-seed-set --out-dir "$(TAIJI_SEED_STAGE3_DIR)" > /dev/null
	{ cd "$(TAIJI_SEED_STAGE0_DIR)" && find . -type f | LC_ALL=C sort | while read f; do shasum -a 256 "$$f"; done; } > "$(TAIJI_SEED_STAGE0_SHA)"
	{ cd "$(TAIJI_SEED_STAGE3_DIR)" && find . -type f | LC_ALL=C sort | while read f; do shasum -a 256 "$$f"; done; } > "$(TAIJI_SEED_STAGE3_SHA)"
	cmp -s "$(TAIJI_SEED_STAGE0_SHA)" "$(TAIJI_SEED_STAGE3_SHA)" || { echo "compiler-core-system-link-exec mismatch: taiji-seed-set stage0/stage3 output" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

obj-file-layout: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" obj-file --in "$(TOOLING_SOURCE)" --target arm64-apple-darwin > "$(OBJ_FILE_STDOUT)"
	cmp -s "$(OBJ_FILE_EXPECTED)" "$(OBJ_FILE_STDOUT)" || { echo "obj-file-layout mismatch: obj-file stdout" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

full-selfhost: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" stage-selfhost-host --out-dir "$(ROOT_DIR)/artifacts/full_selfhost" --target arm64-apple-darwin > "$(FULL_SELFHOST_STDOUT)"
	cmp -s "$(FULL_SELFHOST_EXPECTED)" "$(FULL_SELFHOST_STDOUT)" || { echo "full-selfhost mismatch: stage-selfhost-host output" 1>&2; exit 1; }
	"$(COMPILER_OUT)" program-selfhost-check --out-dir "$(ROOT_DIR)/artifacts/full_selfhost/program_selfhost" --target arm64-apple-darwin > "$(PROGRAM_SELFHOST_STDOUT)"
	cmp -s "$(PROGRAM_SELFHOST_EXPECTED)" "$(PROGRAM_SELFHOST_STDOUT)" || { echo "full-selfhost mismatch: program-selfhost-check output" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

program-selfhost: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" program-selfhost-check --out-dir "$(ROOT_DIR)/artifacts/full_selfhost/program_selfhost" --target arm64-apple-darwin > "$(PROGRAM_SELFHOST_STDOUT)"
	cmp -s "$(PROGRAM_SELFHOST_EXPECTED)" "$(PROGRAM_SELFHOST_STDOUT)" || { echo "program-selfhost mismatch: program-selfhost-check output" 1>&2; exit 1; }
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

clean:
	rm -f "$(BOOTSTRAP_OUT)" "$(COMPILER_OUT)" "$(SELFHOST_STAGE1)" "$(SELFHOST_STAGE1_C)" "$(SELFHOST_STAGE1_EXEC)" "$(SELFHOST_STAGE1_OUT)" "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_STDOUT)" "$(SELFHOST_SYSTEM_LINK_PLAN_SHARED_FILE)" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_STDOUT)" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_FILE)" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_BINARY)" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_BINARY).primary.o" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_BINARY).provider.0.o" "$(SELFHOST_SYSTEM_LINK_EXEC_SHARED_BINARY).provider.1.o" "$(TOOLING_STAGE1)" "$(TOOLING_SOURCE_MANIFEST_STDOUT)" "$(TOOLING_SOURCE_MANIFEST_FILE)" "$(TOOLING_RULE_PACK_STDOUT)" "$(TOOLING_RULE_PACK_FILE)" "$(TOOLING_COMPILER_RULE_PACK_STDOUT)" "$(TOOLING_COMPILER_RULE_PACK_FILE)" "$(TOOLING_RELEASE_STDOUT)" "$(TOOLING_RELEASE_FILE)" "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_STDOUT)" "$(TOOLING_SYSTEM_LINK_PLAN_SHARED_FILE)" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_STDOUT)" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_FILE)" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_BINARY)" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_BINARY).primary.o" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_BINARY).provider.0.o" "$(TOOLING_SYSTEM_LINK_EXEC_SHARED_BINARY).provider.1.o" "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_STDOUT)" "$(TOPOLOGY_SYSTEM_LINK_PLAN_SHARED_FILE)" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_STDOUT)" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_FILE)" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_BINARY)" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_BINARY).primary.o" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_BINARY).provider.0.o" "$(TOPOLOGY_SYSTEM_LINK_EXEC_SHARED_BINARY).provider.1.o" "$(LSMR_ADDRESS_STDOUT)" "$(LSMR_ROUTE_PLAN_STDOUT)" "$(OBJ_FILE_STDOUT)" "$(COMPILER_CORE_RELEASE_STDOUT)" "$(COMPILER_CORE_RELEASE_FILE)" "$(COMPILER_CORE_SYSTEM_LINK_STDOUT)" "$(COMPILER_CORE_SYSTEM_LINK_FILE)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_STDOUT)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_FILE)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY).primary.o" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY).provider.0.o" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY).provider.1.o" "$(TAIJI_SEED_STAGE0_SHA)" "$(TAIJI_SEED_STAGE3_SHA)" "$(NETWORK_SELFHOST_OUT)" "$(COMPILER_CORE_ZRPC_SURFACE_OUT)" "$(V2_NO_POINTER_SURFACE_OUT)" "$(V2_NO_POINTER_COMPILE_FAILURES_OUT)" "$(V2_NO_POINTER_PACKAGE_COMPILE_CLOSURES_OUT)" "$(V2_NO_POINTER_PACKAGE_CLOSURES_OUT)" "$(COMPILER_CORE_NO_POINTER_CLOSURE_OUT)" "$(TOOLING_SELFHOST_OUT)" "$(PROGRAM_SELFHOST_STDOUT)" "$(FULL_SELFHOST_STDOUT)"
	rm -rf "$(TAIJI_SEED_STAGE0_DIR)" "$(TAIJI_SEED_STAGE3_DIR)"
