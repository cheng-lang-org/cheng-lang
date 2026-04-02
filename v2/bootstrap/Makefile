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
TOOLING_ROOT := $(ROOT_DIR)/examples
TOOLING_SOURCE := $(ROOT_DIR)/examples/network_distribution_module.cheng
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
LSMR_ADDRESS_CID := 499128e86e4bf16650ba638299a0f91638a6c5edcc0f3e2674d4785b89632900
LSMR_DEPTH := 4
LSMR_ADDRESS_STDOUT := $(OUT_DIR)/lsmr_address.stdout
LSMR_ADDRESS_EXPECTED := $(ROOT_DIR)/tests/contracts/lsmr_address.expected
LSMR_ROUTE_PLAN_STDOUT := $(OUT_DIR)/lsmr_route_plan.stdout
LSMR_ROUTE_PLAN_EXPECTED := $(ROOT_DIR)/tests/contracts/lsmr_route_plan.expected
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
TOOLING_SELFHOST_OUT := $(OUT_DIR)/tooling_selfhost.out
TOOLING_SELFHOST_EXPECTED := $(ROOT_DIR)/tests/contracts/tooling_selfhost.expected
FULL_SELFHOST_STDOUT := $(OUT_DIR)/full_selfhost.stdout
FULL_SELFHOST_EXPECTED := $(ROOT_DIR)/tests/contracts/full_selfhost.expected

.PHONY: all clean selfhost tooling-selfhost compiler-core-release compiler-core-system-link compiler-core-system-link-exec obj-file-layout lsmr-contracts full-selfhost

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
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

compiler-core-release: $(BOOTSTRAP_OUT) $(COMPILER_OUT)
	"$(COMPILER_OUT)" release-compile --in "$(COMPILER_CORE_SOURCE)" --emit exe --target arm64-apple-darwin --out "$(COMPILER_CORE_RELEASE_FILE)" > "$(COMPILER_CORE_RELEASE_STDOUT)"
	cmp -s "$(COMPILER_CORE_RELEASE_EXPECTED)" "$(COMPILER_CORE_RELEASE_STDOUT)" || { echo "compiler-core-release mismatch: release-compile stdout" 1>&2; exit 1; }
	cmp -s "$(COMPILER_CORE_RELEASE_EXPECTED)" "$(COMPILER_CORE_RELEASE_FILE)" || { echo "compiler-core-release mismatch: release-compile file" 1>&2; exit 1; }
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
	cp -f "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)"
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" status > "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" 2>&1; rc=$$?; printf "status_rc=%d\n" "$$rc" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)"; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" release-compile --in "$(TOOLING_SOURCE)" --emit exe --target arm64-apple-darwin >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" 2>&1; rc=$$?; printf "release_compile_rc=%d\n" "$$rc" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)"; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" lsmr-address --cid "$(LSMR_ADDRESS_CID)" --depth "$(LSMR_DEPTH)" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" 2>&1; rc=$$?; printf "lsmr_address_rc=%d\n" "$$rc" >> "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)"; test "$$rc" -eq 0; }
	cmp -s "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE_EXPECTED)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" || { echo "compiler-core-system-link-exec mismatch: native launch smoke" 1>&2; exit 1; }
	! rg -q "compiler_core_replay_ok|runtime/compiler_core.command_dispatch|native_tooling_dispatch|external_cc_obj" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" || { echo "compiler-core-system-link-exec mismatch: legacy compiler_core dispatch text present" 1>&2; exit 1; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)" trace-phase-space > /dev/null 2>&1; rc=$$?; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)" emit-proof-obligations > /dev/null 2>&1; rc=$$?; test "$$rc" -eq 0; }
	{ "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)" taiji-seed-set > /dev/null 2>&1; rc=$$?; test "$$rc" -eq 0; }
	rm -rf "$(TAIJI_SEED_STAGE0_DIR)" "$(TAIJI_SEED_STAGE3_DIR)"
	"$(COMPILER_OUT)" taiji-seed-set --out-dir "$(TAIJI_SEED_STAGE0_DIR)" > /dev/null
	"$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)" taiji-seed-set --out-dir "$(TAIJI_SEED_STAGE3_DIR)" > /dev/null
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
	"$(BOOTSTRAP_OUT)" check-tree "$(ROOT_DIR)"
	"$(BOOTSTRAP_OUT)" manifest "$(ROOT_DIR)"

clean:
	rm -f "$(BOOTSTRAP_OUT)" "$(COMPILER_OUT)" "$(SELFHOST_STAGE1)" "$(SELFHOST_STAGE1_C)" "$(SELFHOST_STAGE1_EXEC)" "$(SELFHOST_STAGE1_OUT)" "$(TOOLING_STAGE1)" "$(TOOLING_SOURCE_MANIFEST_STDOUT)" "$(TOOLING_SOURCE_MANIFEST_FILE)" "$(TOOLING_RULE_PACK_STDOUT)" "$(TOOLING_RULE_PACK_FILE)" "$(TOOLING_COMPILER_RULE_PACK_STDOUT)" "$(TOOLING_COMPILER_RULE_PACK_FILE)" "$(TOOLING_RELEASE_STDOUT)" "$(TOOLING_RELEASE_FILE)" "$(LSMR_ADDRESS_STDOUT)" "$(LSMR_ROUTE_PLAN_STDOUT)" "$(OBJ_FILE_STDOUT)" "$(COMPILER_CORE_RELEASE_STDOUT)" "$(COMPILER_CORE_RELEASE_FILE)" "$(COMPILER_CORE_SYSTEM_LINK_STDOUT)" "$(COMPILER_CORE_SYSTEM_LINK_FILE)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_STDOUT)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_FILE)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_SMOKE)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_ALIAS)" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY).primary.o" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY).provider.0.o" "$(COMPILER_CORE_SYSTEM_LINK_EXEC_BINARY).provider.1.o" "$(TAIJI_SEED_STAGE0_SHA)" "$(TAIJI_SEED_STAGE3_SHA)" "$(NETWORK_SELFHOST_OUT)" "$(TOOLING_SELFHOST_OUT)" "$(FULL_SELFHOST_STDOUT)"
	rm -rf "$(TAIJI_SEED_STAGE0_DIR)" "$(TAIJI_SEED_STAGE3_DIR)"
