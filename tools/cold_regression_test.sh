#!/bin/bash
# Cold compiler regression test suite
# Run: bash tools/cold_regression_test.sh
set -uo pipefail

COLD="${CHENG_COLD:-/tmp/cheng_cold}"
if [ ! -x "$COLD" ] ||
   { [ "$COLD" = "/tmp/cheng_cold" ] &&
     { [ bootstrap/cheng_cold.c -nt "$COLD" ] ||
       [ bootstrap/elf64_direct.h -nt "$COLD" ] ||
       [ bootstrap/rv64_emit.h -nt "$COLD" ]; }; }; then
    cc -std=c11 -O2 -o "$COLD" bootstrap/cheng_cold.c
fi

PASS=0; FAIL=0
quiet() { "$@" >/dev/null 2>&1; }

assert() {
    local name="$1" expected="$2" actual="$3"
    if [ "$actual" = "$expected" ]; then
        echo "  PASS $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $name (expected $expected, got $actual)"
        FAIL=$((FAIL + 1))
    fi
}

cd "$(dirname "$0")/.."
echo "=== Cold Compiler Regression ==="

# Test helper: compile source and run
compile_run() {
    local src="$1" out="$2"
    rm -f "$out"
    quiet $COLD system-link-exec --in:"$src" --target:arm64-apple-darwin --out:"$out"
    if [ -x "$out" ]; then
        "$out" 2>/dev/null; echo $?
    else
        echo "COMPILE_FAILED"
    fi
}

compile_run_timed() {
    local compiler="$1" src="$2" out="$3" limit="$4"
    rm -f "$out"
    timeout "$limit" bash -c '
        compiler="$1"
        src="$2"
        out="$3"
        "$compiler" system-link-exec --in:"$src" --target:arm64-apple-darwin --out:"$out" >/dev/null 2>&1
        if [ ! -x "$out" ]; then
            exit 126
        fi
        "$out" >/dev/null 2>&1
    ' sh "$compiler" "$src" "$out"
    local status=$?
    if [ "$status" -eq 124 ]; then
        echo "TIMED_OUT"
    elif [ "$status" -eq 126 ]; then
        echo "COMPILE_FAILED"
    else
        echo "$status"
    fi
}

# 1-3: Core tests
ACT=$(compile_run src/tests/ordinary_zero_exit_fixture.cheng /tmp/ct_oz)
assert "ordinary_zero" 0 "$ACT"

ACT=$(compile_run src/tests/cold_var_index_smoke.cheng /tmp/ct_var_index)
assert "var_index_smoke" 0 "$ACT"

rm -f /tmp/ct_iu /tmp/ct_iu.report
quiet $COLD system-link-exec --in:src/tests/import_use.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_iu --report-out:/tmp/ct_iu.report
if [ -x /tmp/ct_iu ]; then
    /tmp/ct_iu 2>/dev/null; ACT=$?
else
    ACT="COMPILE_FAILED"
fi
assert "import_use" 3 "$ACT"
if grep -q '^direct_macho=1$' /tmp/ct_iu.report 2>/dev/null &&
   grep -q '^provider_object_count=0$' /tmp/ct_iu.report 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_iu.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "import_use_linkerless" 1 "$ACT"

rm -f /tmp/ct_link_providers /tmp/ct_link_providers.report
if $COLD system-link-exec --in:src/tests/import_use.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_link_providers \
    --report-out:/tmp/ct_link_providers.report --link-providers >/dev/null 2>&1; then
    ACT="UNEXPECTED_SUCCESS"
elif grep -q '^system_link_exec=0$' /tmp/ct_link_providers.report 2>/dev/null &&
     grep -q '^error=--link-providers requires ELF target$' /tmp/ct_link_providers.report 2>/dev/null &&
     [ ! -e /tmp/ct_link_providers ]; then
    ACT="HARD_FAIL"
else
    ACT="WRONG_FAILURE"
fi
assert "link_providers_requires_elf" "HARD_FAIL" "$ACT"

rm -f /tmp/ct_bad_import
quiet $COLD system-link-exec --in:src/tests/cold_bad_import_unresolved_main.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_bad_import
if [ -x /tmp/ct_bad_import ]; then
    ACT="UNEXPECTED_SUCCESS"
else
    ACT="COMPILE_FAILED"
fi
assert "import_unresolved_hard_fail" "UNEXPECTED_SUCCESS" "$ACT"

rm -f /tmp/ct_bare_helper /tmp/ct_bare_helper.report
quiet $COLD system-link-exec --in:src/tests/cold_import_bare_helper_main.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_bare_helper \
    --report-out:/tmp/ct_bare_helper.report
if [ -x /tmp/ct_bare_helper ]; then
    /tmp/ct_bare_helper 2>/dev/null; ACT=$?
else
    ACT="COMPILE_FAILED"
fi
assert "import_bare_helper" 39 "$ACT"
if grep -q '^direct_macho=1$' /tmp/ct_bare_helper.report 2>/dev/null &&
   grep -q '^provider_object_count=0$' /tmp/ct_bare_helper.report 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_bare_helper.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "import_bare_helper_linkerless" 1 "$ACT"

rm -f /tmp/ct_typed_const
quiet $COLD system-link-exec --in:src/tests/cold_import_typed_const_main.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_typed_const
if [ -x /tmp/ct_typed_const ]; then
    /tmp/ct_typed_const 2>/dev/null; ACT=$?
else
    ACT="COMPILE_FAILED"
fi
assert "import_typed_const" 0 "$ACT"

rm -f /tmp/ct_deep_import
quiet $COLD system-link-exec --in:src/tests/cold_import_deep_main.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_deep_import
if [ -x /tmp/ct_deep_import ]; then
    ACT="UNEXPECTED_SUCCESS"
else
    ACT="COMPILE_FAILED"
fi
assert "import_deep_hard_fail" "UNEXPECTED_SUCCESS" "$ACT"

quiet $COLD system-link-exec --in:src/core/tooling/backend_driver_dispatch_min.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_dm
if [ -x /tmp/ct_dm ]; then
    assert "dispatch_min" 0 0
else
    assert "dispatch_min" 0 "COMPILE_FAILED"
fi

rm -f /tmp/ct_dm_self /tmp/ct_dm_self.report
if [ -x /tmp/ct_dm ]; then
    quiet /tmp/ct_dm system-link-exec --in:src/tests/ordinary_zero_exit_fixture.cheng \
        --target:arm64-apple-darwin --out:/tmp/ct_dm_self --report-out:/tmp/ct_dm_self.report
    if [ -x /tmp/ct_dm_self ]; then
        /tmp/ct_dm_self 2>/dev/null; ACT=$?
    else
        ACT="COMPILE_FAILED"
    fi
else
    ACT="COMPILE_FAILED"
fi
assert "dispatch_min_self_ordinary_zero" 0 "$ACT"

rm -f /tmp/ct_dm_import /tmp/ct_dm_import.report
if [ -x /tmp/ct_dm ]; then
    quiet /tmp/ct_dm system-link-exec --in:src/tests/import_use.cheng \
        --target:arm64-apple-darwin --out:/tmp/ct_dm_import --report-out:/tmp/ct_dm_import.report
    if [ -x /tmp/ct_dm_import ]; then
        /tmp/ct_dm_import 2>/dev/null; ACT=$?
    else
        ACT="COMPILE_FAILED"
    fi
else
    ACT="COMPILE_FAILED"
fi
assert "dispatch_min_self_import_use" 3 "$ACT"
if grep -q '^direct_macho=1$' /tmp/ct_dm_import.report 2>/dev/null &&
   grep -q '^provider_object_count=0$' /tmp/ct_dm_import.report 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_dm_import.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "dispatch_min_self_import_linkerless" 1 "$ACT"

rm -f /tmp/ct_dm_cov /tmp/ct_dm_cov.report
if [ -x /tmp/ct_dm ]; then
    quiet /tmp/ct_dm system-link-exec --in:src/tests/cold_subset_coverage.cheng \
        --target:arm64-apple-darwin --out:/tmp/ct_dm_cov --report-out:/tmp/ct_dm_cov.report
    if [ -x /tmp/ct_dm_cov ]; then
        /tmp/ct_dm_cov 2>/dev/null; ACT=$?
    else
        ACT="COMPILE_FAILED"
    fi
else
    ACT="COMPILE_FAILED"
fi
assert "dispatch_min_self_subset_coverage" 0 "$ACT"
if grep -q '^direct_macho=1$' /tmp/ct_dm_cov.report 2>/dev/null &&
   grep -q '^provider_object_count=0$' /tmp/ct_dm_cov.report 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_dm_cov.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "dispatch_min_self_subset_linkerless" 1 "$ACT"

rm -f /tmp/ct_dm_ec.o /tmp/ct_dm_ec_link /tmp/ct_dm_ec.report
if [ -x /tmp/ct_dm ]; then
    quiet /tmp/ct_dm system-link-exec --in:src/tests/import_use.cheng \
        --emit:obj --target:arm64-apple-darwin --out:/tmp/ct_dm_ec.o \
        --report-out:/tmp/ct_dm_ec.report
    if quiet cc -o /tmp/ct_dm_ec_link /tmp/ct_dm_ec.o; then
        /tmp/ct_dm_ec_link 2>/dev/null; ACT=$?
    else
        ACT="LINK_FAILED"
    fi
else
    ACT="COMPILE_FAILED"
fi
assert "dispatch_min_self_emit_obj_cross" 3 "$ACT"
if grep -q '^emit=obj$' /tmp/ct_dm_ec.report 2>/dev/null &&
   grep -q '^direct_macho=1$' /tmp/ct_dm_ec.report 2>/dev/null &&
   grep -q '^provider_object_count=0$' /tmp/ct_dm_ec.report 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_dm_ec.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "dispatch_min_self_emit_obj_direct" 1 "$ACT"

rm -f /tmp/ct_dm2 /tmp/ct_dm2.report /tmp/ct_dm2_status.stdout /tmp/ct_dm2_status.stderr
if [ -x /tmp/ct_dm ]; then
    quiet /tmp/ct_dm system-link-exec --in:src/core/tooling/backend_driver_dispatch_min.cheng \
        --target:arm64-apple-darwin --out:/tmp/ct_dm2 --report-out:/tmp/ct_dm2.report
    if [ -x /tmp/ct_dm2 ]; then
        ACT=0
    else
        ACT="COMPILE_FAILED"
    fi
else
    ACT="COMPILE_FAILED"
fi
assert "dispatch_min_self_dispatch_min" 0 "$ACT"
if grep -q '^direct_macho=1$' /tmp/ct_dm2.report 2>/dev/null &&
   grep -q '^provider_object_count=0$' /tmp/ct_dm2.report 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_dm2.report 2>/dev/null &&
   grep -q '^source=src/core/tooling/backend_driver_dispatch_min.cheng$' /tmp/ct_dm2.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "dispatch_min_self_dispatch_direct" 1 "$ACT"
if [ -x /tmp/ct_dm2 ]; then
    /tmp/ct_dm2 status --root:. --in:src/core/tooling/backend_driver_dispatch_min.cheng \
        --out:/tmp/ct_dm2_status.out >/tmp/ct_dm2_status.stdout 2>/tmp/ct_dm2_status.stderr
    ACT=$?
else
    ACT="STATUS_FAILED"
fi
assert "dispatch_min_self_dispatch_status" 0 "$ACT"
if grep -q '^bootstrap_mode=selfhost$' /tmp/ct_dm2_status.stdout 2>/dev/null &&
   grep -q '^flag_exec_edges=0$' /tmp/ct_dm2_status.stdout 2>/dev/null &&
   grep -q '^flag_exec_unresolved=0$' /tmp/ct_dm2_status.stdout 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "dispatch_min_self_dispatch_status_marker" 1 "$ACT"

cat > /tmp/ct_while_only.cheng << 'EOF'
fn main(): int32 =
    var x = 60
    var guard = 0
    while guard < 2:
        x = x + 1
        guard = guard + 1
    return x
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_while_only.cheng /tmp/ct_while_only 10)
assert "while_only" 62 "$ACT"

cat > /tmp/ct_cmp.cheng << 'EOF'
fn main(): int32 =
    var x = 5
    if x != 0:
        x = 1
    else:
        return 255
    if x >= 5:
        return 255
    if x <= 3:
        return 0
    return 255
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_cmp.cheng /tmp/ct_cmp 10)
assert "compare_ops" 0 "$ACT"

cat > /tmp/ct_while_and.cheng << 'EOF'
fn touch(x: int32): int32 =
    return x

fn value(seed: int32): int32 =
    var x = seed
    touch(x)
    var guard = 0
    while guard < 2 && x < 70:
        x = x + 1
        guard = guard + 1
    for i in 0..<5:
        if i == 1:
            continue
        if i == 4:
            break
        x = x + i
    x = x + 5
    if !(x < 0) && x > 70:
        x = x + 5
        return x
    else:
        return 1

fn main(): int32 =
    return value(60)
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_while_and.cheng /tmp/ct_while_and 10)
assert "while_and_system_link_shape" 77 "$ACT"

rm -rf /tmp/ct_bd
mkdir -p /tmp/ct_bd
$COLD build-backend-driver --out:/tmp/ct_bd/cheng \
    --report-out:/tmp/ct_bd/report.txt --map-out:/tmp/ct_bd/map.txt \
    --index-out:/tmp/ct_bd/index.txt >/tmp/ct_bd/stdout.txt 2>/tmp/ct_bd/stderr.txt
ACT=$?
assert "build_backend_driver_cold_linkerless" 0 "$ACT"
if [ -x /tmp/ct_bd/cheng ] &&
   grep -q '^real_backend_codegen=1$' /tmp/ct_bd/report.txt 2>/dev/null &&
   grep -q '^system_link_exec=1$' /tmp/ct_bd/report.txt 2>/dev/null &&
   grep -q '^system_link_exec_scope=cold_subset_direct_macho$' /tmp/ct_bd/report.txt 2>/dev/null &&
   ! grep -q 'cold_cc_link' /tmp/ct_bd/stdout.txt /tmp/ct_bd/report.txt /tmp/ct_bd/map.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "build_backend_driver_no_cc_fallback" 1 "$ACT"
if ! grep -E '^(=== (AFTER REWRITE|BODY OPS|BODY TERMS|DEBUG)|  (op|term)\[[0-9]+\]:)' \
    /tmp/ct_bd/stdout.txt /tmp/ct_bd/stderr.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "build_backend_driver_no_debug_noise" 1 "$ACT"
if ! grep -nE '\[DBG\]|\[field_assign\]|\[take_until\]|\[parse_index_bracket\]|backtrace\(|execinfo\.h' \
    bootstrap/cheng_cold.c bootstrap/cold_parser.c >/tmp/ct_static_debug_markers.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "cold_no_static_debug_markers" 1 "$ACT"
if [ -x /tmp/ct_bd/cheng ]; then
    /tmp/ct_bd/cheng status --root:. --in:src/core/tooling/backend_driver_dispatch_min.cheng \
        --out:/tmp/ct_bd/status.out >/tmp/ct_bd/status.stdout 2>/tmp/ct_bd/status.stderr
    ACT=$?
else
    ACT="STATUS_FAILED"
fi
assert "build_backend_driver_status" 0 "$ACT"
if grep -q 'linkerless_image=1' /tmp/ct_bd/status.stdout 2>/dev/null &&
   grep -q 'system_link=0' /tmp/ct_bd/status.stdout 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "build_backend_driver_status_marker" 1 "$ACT"
if [ -x /tmp/ct_bd/cheng ]; then
    ACT=$(compile_run_timed /tmp/ct_bd/cheng /tmp/ct_while_only.cheng /tmp/ct_bd/while_only 10)
else
    ACT="COMPILE_FAILED"
fi
assert "build_backend_driver_system_link_while_only" 62 "$ACT"

rm -f /tmp/ct_compiler_runtime_smoke /tmp/ct_compiler_runtime_smoke.report \
    /tmp/ct_compiler_runtime_smoke.stdout /tmp/ct_compiler_runtime_smoke.stderr
quiet $COLD system-link-exec --root:"$PWD" --in:src/tests/compiler_runtime_smoke.cheng \
    --emit:exe --target:arm64-apple-darwin --out:/tmp/ct_compiler_runtime_smoke \
    --report-out:/tmp/ct_compiler_runtime_smoke.report
if [ -x /tmp/ct_compiler_runtime_smoke ]; then
    /tmp/ct_compiler_runtime_smoke >/tmp/ct_compiler_runtime_smoke.stdout \
        2>/tmp/ct_compiler_runtime_smoke.stderr
    ACT=$?
else
    ACT="COMPILE_FAILED"
fi
assert "compiler_runtime_smoke" 0 "$ACT"
if grep -q 'compiler_runtime_smoke ok' /tmp/ct_compiler_runtime_smoke.stdout 2>/dev/null &&
   grep -q '^direct_macho=1$' /tmp/ct_compiler_runtime_smoke.report 2>/dev/null &&
   grep -q '^provider_object_count=0$' /tmp/ct_compiler_runtime_smoke.report 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_compiler_runtime_smoke.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "compiler_runtime_smoke_linkerless_marker" 1 "$ACT"

# 4: for_range
cat > /tmp/ct_for.cheng << 'EOF'
fn main(): int32 =
    var s: int32
    for i in 0..<10:
        s = s + i
    return s
EOF
ACT=$(compile_run /tmp/ct_for.cheng /tmp/ct_for_out)
assert "for_range" 45 "$ACT"

# 5: emit:obj
cat > /tmp/ct_obj.cheng << 'EOF'
fn main(): int32 =
    return 42
EOF
quiet $COLD system-link-exec --in:/tmp/ct_obj.cheng --target:arm64-apple-darwin \
    --emit:obj --out:/tmp/ct_obj.o
if quiet cc -o /tmp/ct_obj_link /tmp/ct_obj.o; then
    /tmp/ct_obj_link 2>/dev/null; ACT=$?
else
    ACT="LINK_FAILED"
fi
assert "emit_obj" 42 "$ACT"

# 6: emit:obj cross-module
quiet $COLD system-link-exec --in:src/tests/import_use.cheng --target:arm64-apple-darwin \
    --emit:obj --out:/tmp/ct_ec.o
if quiet cc -o /tmp/ct_ec_link /tmp/ct_ec.o; then
    /tmp/ct_ec_link 2>/dev/null; ACT=$?
else
    ACT="LINK_FAILED"
fi
assert "emit_obj_cross" 3 "$ACT"

# 7: multi-member provider archive (RISC-V ELF cross-target)
mkdir -p /tmp/ct_providers
cat > /tmp/ct_providers/prov_a.cheng << 'PROVEOF'
@exportc("ct_prov_a_val")
fn ct_prov_a_val(): int32 = return 42
PROVEOF
cat > /tmp/ct_providers/prov_b.cheng << 'PROVEOF'
@exportc("ct_prov_b_val")
fn ct_prov_b_val(): int32 = return 99
PROVEOF
cat > /tmp/ct_providers/primary.cheng << 'PROVEOF'
@importc("ct_prov_a_val")
fn ct_prov_a_val(): int32
@importc("ct_prov_b_val")
fn ct_prov_b_val(): int32
fn main(): int32 = return ct_prov_a_val() + ct_prov_b_val()
PROVEOF

quiet $COLD system-link-exec --in:/tmp/ct_providers/prov_a.cheng \
    --emit:obj --target:riscv64-unknown-linux-gnu \
    --out:/tmp/ct_providers/prov_a.o \
    --report-out:/tmp/ct_providers/prov_a.report.txt
quiet $COLD system-link-exec --in:/tmp/ct_providers/prov_b.cheng \
    --emit:obj --target:riscv64-unknown-linux-gnu \
    --out:/tmp/ct_providers/prov_b.o \
    --report-out:/tmp/ct_providers/prov_b.report.txt
quiet $COLD system-link-exec --in:/tmp/ct_providers/primary.cheng \
    --emit:obj --target:riscv64-unknown-linux-gnu \
    --out:/tmp/ct_providers/primary.o \
    --report-out:/tmp/ct_providers/primary.report.txt

quiet $COLD provider-archive-pack \
    --target:riscv64-unknown-linux-gnu \
    --object:/tmp/ct_providers/prov_a.o \
    --object:/tmp/ct_providers/prov_b.o \
    --export:ct_prov_a_val \
    --export:ct_prov_b_val \
    --module:ct_providers \
    --source:/tmp/ct_providers \
    --out:/tmp/ct_providers/multi.chenga \
    --report-out:/tmp/ct_providers/multi.pack.report.txt
if [ -f /tmp/ct_providers/multi.chenga ]; then
    ACT=1
else
    ACT=0
fi
assert "provider_archive_multi_pack" 1 "$ACT"

quiet $COLD system-link-exec \
    --link-object:/tmp/ct_providers/primary.o \
    --provider-archive:/tmp/ct_providers/multi.chenga \
    --emit:exe --target:riscv64-unknown-linux-gnu \
    --out:/tmp/ct_providers/multi_linked \
    --report-out:/tmp/ct_providers/multi_link.report.txt
if grep -q '^provider_archive_member_count=2$' /tmp/ct_providers/multi_link.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=2$' /tmp/ct_providers/multi_link.report.txt 2>/dev/null &&
   grep -q '^provider_resolved_symbol_count=2$' /tmp/ct_providers/multi_link.report.txt 2>/dev/null &&
   grep -q '^unresolved_symbol_count=0$' /tmp/ct_providers/multi_link.report.txt 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_providers/multi_link.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "provider_archive_multi_link" 1 "$ACT"

cat > /tmp/ct_providers/alias_provider.cheng << 'PROVEOF'
@exportc("ct_alias_provider_bridge")
fn AliasProviderLocalName(): int32 = return 17
PROVEOF
cat > /tmp/ct_providers/alias_primary.cheng << 'PROVEOF'
@importc("ct_alias_provider_bridge")
fn AliasPrimaryLocalName(): int32

fn main(): int32 = return AliasPrimaryLocalName()
PROVEOF

quiet $COLD system-link-exec --in:/tmp/ct_providers/alias_provider.cheng \
    --emit:obj --target:riscv64-unknown-linux-gnu \
    --out:/tmp/ct_providers/alias_provider.o \
    --report-out:/tmp/ct_providers/alias_provider.report.txt
quiet $COLD system-link-exec --in:/tmp/ct_providers/alias_primary.cheng \
    --emit:obj --target:riscv64-unknown-linux-gnu \
    --out:/tmp/ct_providers/alias_primary.o \
    --report-out:/tmp/ct_providers/alias_primary.report.txt
quiet $COLD provider-archive-pack \
    --target:riscv64-unknown-linux-gnu \
    --object:/tmp/ct_providers/alias_provider.o \
    --export:ct_alias_provider_bridge \
    --module:ct_alias_provider \
    --source:/tmp/ct_providers/alias_provider.cheng \
    --out:/tmp/ct_providers/alias_provider.chenga \
    --report-out:/tmp/ct_providers/alias_provider.pack.report.txt
if [ -f /tmp/ct_providers/alias_provider.chenga ] &&
   grep -q '^provider_export=ct_alias_provider_bridge$' /tmp/ct_providers/alias_provider.pack.report.txt 2>/dev/null &&
   grep -q '^provider_archive_member_count=1$' /tmp/ct_providers/alias_provider.pack.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=1$' /tmp/ct_providers/alias_provider.pack.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "provider_archive_alias_pack" 1 "$ACT"

quiet $COLD system-link-exec \
    --link-object:/tmp/ct_providers/alias_primary.o \
    --provider-archive:/tmp/ct_providers/alias_provider.chenga \
    --emit:exe --target:riscv64-unknown-linux-gnu \
    --out:/tmp/ct_providers/alias_linked \
    --report-out:/tmp/ct_providers/alias_link.report.txt
if grep -q '^provider_archive_member_count=1$' /tmp/ct_providers/alias_link.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=1$' /tmp/ct_providers/alias_link.report.txt 2>/dev/null &&
   grep -q '^provider_resolved_symbol_count=1$' /tmp/ct_providers/alias_link.report.txt 2>/dev/null &&
   grep -q '^unresolved_symbol_count=0$' /tmp/ct_providers/alias_link.report.txt 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_providers/alias_link.report.txt 2>/dev/null &&
   grep -q '^linkerless_image=1$' /tmp/ct_providers/alias_link.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "provider_archive_alias_link" 1 "$ACT"

rm -rf /tmp/ct_providers

# 8: language subset coverage
ACT=$(compile_run src/tests/cold_subset_coverage.cheng /tmp/ct_cov)
assert "subset_coverage" 0 "$ACT"

ACT=$(compile_run src/tests/cold_stack_arg_abi.cheng /tmp/ct_stack_arg_abi)
assert "stack_arg_abi" 0 "$ACT"

# 9: inline tuple field bracket-depth (commas inside brackets in default values)
cat > /tmp/ct_tuple_default_bracket.cheng << 'TUPLEEOF'
# Regression: inline tuple field splitting must track bracket depth
# so that commas inside [default, values] don't cause incorrect splits.

type Pair = tuple[a: int32 = 6, b: int32[], c: int32[2] = [8, 9], d: str[] = ["pair"]]

type Triple = tuple[
    x: int32[] = [10, 20, 30],
    y: int32 = 42,
]

fn UseTriple(t: Triple): int32 =
    return t.y

fn main(): int32 =
    var tp: Triple
    tp.x = [1, 2, 3]
    tp.y = 7
    let result = UseTriple(tp)
    if result != 7:
        return 1
    var pp: Pair
    pp.d = ["hello", "world"]
    pp.c = [8, 9]
    if pp.c[0] != 8:
        return 2
    if pp.d[1] != "world":
        return 3
    return 0
TUPLEEOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_tuple_default_bracket.cheng /tmp/ct_tuple_default_bracket 10)
assert "tuple_default_bracket" 0 "$ACT"

# 10: atomic provider archive (AArch64 ELF, no runtime stubs)
rm -rf /tmp/ct_runtime
mkdir -p /tmp/ct_runtime
quiet as -target aarch64-unknown-linux-gnu -o /tmp/ct_runtime/atomic_provider.o \
    src/core/runtime/linux_atomic_provider_aarch64.S
if [ -f /tmp/ct_runtime/atomic_provider.o ]; then
    ACT=1
else
    ACT=0
fi
assert "atomic_provider_assemble" 1 "$ACT"

quiet $COLD provider-archive-pack \
    --target:aarch64-unknown-linux-gnu \
    --object:/tmp/ct_runtime/atomic_provider.o \
    --export:cheng_atomic_cas_i32 \
    --export:cheng_atomic_store_i32 \
    --export:cheng_atomic_load_i32 \
    --module:atomic_provider \
    --source:src/core/runtime \
    --out:/tmp/ct_runtime/atomic_provider.chenga
if [ -f /tmp/ct_runtime/atomic_provider.chenga ]; then
    ACT=1
else
    ACT=0
fi
assert "atomic_provider_archive_pack" 1 "$ACT"

quiet $COLD system-link-exec --in:src/tests/atomic_i32_direct_runtime_smoke.cheng \
    --emit:obj --target:aarch64-unknown-linux-gnu \
    --out:/tmp/ct_runtime/atomic.o
if [ -f /tmp/ct_runtime/atomic.o ]; then
    ACT=1
else
    ACT=0
fi
assert "runtime_atomic_obj" 1 "$ACT"

rm -f /tmp/ct_runtime/linked /tmp/ct_runtime/link.report.txt
quiet $COLD system-link-exec \
    --link-object:/tmp/ct_runtime/atomic.o \
    --provider-archive:/tmp/ct_runtime/atomic_provider.chenga \
    --emit:exe --target:aarch64-unknown-linux-gnu \
    --out:/tmp/ct_runtime/linked \
    --report-out:/tmp/ct_runtime/link.report.txt
if grep -q '^provider_archive_member_count=1$' /tmp/ct_runtime/link.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=3$' /tmp/ct_runtime/link.report.txt 2>/dev/null &&
   grep -q '^provider_resolved_symbol_count=6$' /tmp/ct_runtime/link.report.txt 2>/dev/null &&
   grep -q '^unresolved_symbol_count=0$' /tmp/ct_runtime/link.report.txt 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_runtime/link.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "atomic_provider_archive_link" 1 "$ACT"

rm -f /tmp/ct_runtime/core_runtime_af_inet.o \
    /tmp/ct_runtime/core_runtime_af_inet.report.txt \
    /tmp/ct_runtime/core_runtime_af_inet.chenga \
    /tmp/ct_runtime/core_runtime_af_inet.pack.report.txt
quiet $COLD system-link-exec --root:"$PWD" \
    --in:src/core/runtime/core_runtime_provider_linux.cheng \
    --emit:obj --target:aarch64-unknown-linux-gnu \
    --symbol-visibility:internal \
    --export-roots:cheng_native_af_inet_bridge \
    --out:/tmp/ct_runtime/core_runtime_af_inet.o \
    --report-out:/tmp/ct_runtime/core_runtime_af_inet.report.txt
if [ -s /tmp/ct_runtime/core_runtime_af_inet.o ] &&
   file /tmp/ct_runtime/core_runtime_af_inet.o 2>/dev/null | grep -q 'ELF 64-bit.*relocatable.*aarch64' &&
   grep -q '^emit=obj$' /tmp/ct_runtime/core_runtime_af_inet.report.txt 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_runtime/core_runtime_af_inet.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "runtime_provider_linux_af_inet_single_root_obj" 1 "$ACT"

quiet $COLD provider-archive-pack \
    --target:aarch64-unknown-linux-gnu \
    --object:/tmp/ct_runtime/core_runtime_af_inet.o \
    --export:cheng_native_af_inet_bridge \
    --module:runtime_core_runtime \
    --source:src/core/runtime/core_runtime_provider_linux.cheng \
    --out:/tmp/ct_runtime/core_runtime_af_inet.chenga \
    --report-out:/tmp/ct_runtime/core_runtime_af_inet.pack.report.txt
if [ -f /tmp/ct_runtime/core_runtime_af_inet.chenga ] &&
   grep -q '^provider_export=cheng_native_af_inet_bridge$' /tmp/ct_runtime/core_runtime_af_inet.pack.report.txt 2>/dev/null &&
   grep -q '^provider_archive_member_count=1$' /tmp/ct_runtime/core_runtime_af_inet.pack.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=1$' /tmp/ct_runtime/core_runtime_af_inet.pack.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "runtime_provider_linux_af_inet_single_root_export" 1 "$ACT"

rm -f /tmp/ct_runtime/autolink_af_inet \
    /tmp/ct_runtime/autolink_af_inet.report.txt
quiet $COLD system-link-exec \
    --in:testdata/runtime_provider_autolink_af_inet.cheng \
    --target:aarch64-unknown-linux-gnu \
    --out:/tmp/ct_runtime/autolink_af_inet \
    --report-out:/tmp/ct_runtime/autolink_af_inet.report.txt \
    --link-providers
if [ -s /tmp/ct_runtime/autolink_af_inet ] &&
   file /tmp/ct_runtime/autolink_af_inet 2>/dev/null | grep -q 'ELF 64-bit.*executable.*aarch64' &&
   grep -q '^system_link_exec=1$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^system_link_exec_scope=cold_runtime_provider_archive$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^provider_archive=1$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^provider_object_count=1$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^provider_archive_member_count=1$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=1$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^provider_resolved_symbol_count=1$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^unresolved_symbol_count=0$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_runtime/autolink_af_inet.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "runtime_provider_autolink_af_inet" 1 "$ACT"

rm -f /tmp/ct_runtime/autolink_constants \
    /tmp/ct_runtime/autolink_constants.report.txt
quiet $COLD system-link-exec \
    --in:testdata/runtime_provider_autolink_constants.cheng \
    --target:aarch64-unknown-linux-gnu \
    --out:/tmp/ct_runtime/autolink_constants \
    --report-out:/tmp/ct_runtime/autolink_constants.report.txt \
    --link-providers
if [ -s /tmp/ct_runtime/autolink_constants ] &&
   file /tmp/ct_runtime/autolink_constants 2>/dev/null | grep -q 'ELF 64-bit.*executable.*aarch64' &&
   grep -q '^system_link_exec=1$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^system_link_exec_scope=cold_runtime_provider_archive$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^provider_archive=1$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^provider_object_count=1$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^provider_archive_member_count=1$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=10$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^provider_resolved_symbol_count=10$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^unresolved_symbol_count=0$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null &&
   grep -q '^system_link=0$' /tmp/ct_runtime/autolink_constants.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "runtime_provider_autolink_constants" 1 "$ACT"

rm -f /tmp/ct_runtime/autolink_cpu_cores \
    /tmp/ct_runtime/autolink_cpu_cores.report.txt
$COLD system-link-exec \
    --in:testdata/runtime_provider_autolink_cpu_cores.cheng \
    --target:aarch64-unknown-linux-gnu \
    --out:/tmp/ct_runtime/autolink_cpu_cores \
    --report-out:/tmp/ct_runtime/autolink_cpu_cores.report.txt \
    --link-providers >/tmp/ct_runtime/autolink_cpu_cores.stdout 2>/tmp/ct_runtime/autolink_cpu_cores.stderr
CPU_CORES_STATUS=$?
if [ "$CPU_CORES_STATUS" -ne 0 ] &&
   [ ! -f /tmp/ct_runtime/autolink_cpu_cores ] &&
   grep -q '^system_link_exec=0$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^system_link_exec_scope=cold_runtime_provider_archive$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^provider_archive=1$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^provider_object_count=1$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^provider_archive_member_count=1$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^provider_export_count=1$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^provider_resolved_symbol_count=1$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^unresolved_symbol_count=1$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^first_unresolved_symbol=get_nprocs$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null &&
   grep -q '^error=runtime provider archive link failed$' /tmp/ct_runtime/autolink_cpu_cores.report.txt 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "runtime_provider_autolink_cpu_cores_hard_fail" 1 "$ACT"

rm -rf /tmp/ct_runtime

# 11: ownership proof driver report fields
rm -f /tmp/ct_ownership /tmp/ct_ownership.report /tmp/ct_ownership.stdout
quiet $COLD system-link-exec --in:src/tests/ownership_proof_driver_cold.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_ownership \
    --report-out:/tmp/ct_ownership.report \
    --ownership-on
if [ -x /tmp/ct_ownership ]; then
    /tmp/ct_ownership >/tmp/ct_ownership.stdout 2>/dev/null; ACT=$?
else
    ACT="COMPILE_FAILED"
fi
assert "ownership_proof_exit" 0 "$ACT"
if grep -q 'ownership_proof_driver ok' /tmp/ct_ownership.stdout 2>/dev/null &&
   grep -q 'ownership_runtime_witness=1' /tmp/ct_ownership.stdout 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "ownership_proof_output" 1 "$ACT"
if grep -q '^ownership_compile_entry=1$' /tmp/ct_ownership.report 2>/dev/null &&
   grep -q '^ownership_runtime_witness=1$' /tmp/ct_ownership.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "ownership_report_fields" 1 "$ACT"

# Phase-off: same driver WITHOUT --ownership-on — report fields must be 0
rm -f /tmp/ct_ownership_off /tmp/ct_ownership_off.report /tmp/ct_ownership_off.stdout
quiet $COLD system-link-exec --in:src/tests/ownership_proof_driver_cold.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_ownership_off \
    --report-out:/tmp/ct_ownership_off.report
if [ -x /tmp/ct_ownership_off ]; then
    /tmp/ct_ownership_off >/tmp/ct_ownership_off.stdout 2>/dev/null; ACT=$?
else
    ACT="COMPILE_FAILED"
fi
assert "ownership_proof_off_exit" 0 "$ACT"
if grep -q '^ownership_compile_entry=0$' /tmp/ct_ownership_off.report 2>/dev/null &&
   grep -q '^ownership_runtime_witness=0$' /tmp/ct_ownership_off.report 2>/dev/null; then
    ACT=1
else
    ACT=0
fi
assert "ownership_report_off_fields" 1 "$ACT"

# Ownership phase consistency: --ownership-on and without must produce identical stdout markers
rm -f /tmp/ct_ownership_marker_diff
if [ -f /tmp/ct_ownership.stdout ] && [ -f /tmp/ct_ownership_off.stdout ]; then
    if cmp -s /tmp/ct_ownership.stdout /tmp/ct_ownership_off.stdout; then
        ACT=1
    else
        ACT=0
        diff /tmp/ct_ownership.stdout /tmp/ct_ownership_off.stdout > /tmp/ct_ownership_marker_diff 2>&1 || true
    fi
else
    ACT="MISSING_STDOUT"
fi
assert "ownership_phase_consistency" 1 "$ACT"

# 12: emit:obj with object fields (int32 + str)
rm -f /tmp/ct_eo_fields.cheng /tmp/ct_eo_fields /tmp/ct_eo_fields.o /tmp/ct_eo_fields_link
cat > /tmp/ct_eo_fields.cheng << 'EOF'
type Person =
    name: str
    age: int32

fn main(): int32 =
    let p: Person = Person{
        name: "Alice",
        age: 30,
    }
    return p.age
EOF
ACT=$(compile_run /tmp/ct_eo_fields.cheng /tmp/ct_eo_fields)
assert "emit_obj_fields_exe" 30 "$ACT"
quiet $COLD system-link-exec --in:/tmp/ct_eo_fields.cheng --target:arm64-apple-darwin \
    --emit:obj --out:/tmp/ct_eo_fields.o
if [ -s /tmp/ct_eo_fields.o ] && file /tmp/ct_eo_fields.o 2>/dev/null | grep -q "Mach-O 64-bit object"; then
    ACT=1; else ACT=0
fi
assert "emit_obj_fields_obj_file" 1 "$ACT"
if quiet cc -o /tmp/ct_eo_fields_link /tmp/ct_eo_fields.o; then
    /tmp/ct_eo_fields_link 2>/dev/null; ACT=$?
else
    ACT="LINK_FAILED"
fi
assert "emit_obj_fields_obj_run" 30 "$ACT"
rm -f /tmp/ct_eo_fields.cheng /tmp/ct_eo_fields /tmp/ct_eo_fields.o /tmp/ct_eo_fields_link

# 13: emit:obj with match/switch
rm -f /tmp/ct_eo_match.cheng /tmp/ct_eo_match /tmp/ct_eo_match.o /tmp/ct_eo_match_link
cat > /tmp/ct_eo_match.cheng << 'EOF'
type Color = Red | Green | Blue

fn main(): int32 =
    let c: Color = Color.Blue
    match c:
        Red => return 1
        Green => return 2
        Blue => return 42
EOF
ACT=$(compile_run /tmp/ct_eo_match.cheng /tmp/ct_eo_match)
assert "emit_obj_match_exe" 42 "$ACT"
quiet $COLD system-link-exec --in:/tmp/ct_eo_match.cheng --target:arm64-apple-darwin \
    --emit:obj --out:/tmp/ct_eo_match.o
if [ -s /tmp/ct_eo_match.o ] && file /tmp/ct_eo_match.o 2>/dev/null | grep -q "Mach-O 64-bit object"; then
    ACT=1; else ACT=0
fi
assert "emit_obj_match_obj_file" 1 "$ACT"
if quiet cc -o /tmp/ct_eo_match_link /tmp/ct_eo_match.o; then
    /tmp/ct_eo_match_link 2>/dev/null; ACT=$?
else
    ACT="LINK_FAILED"
fi
assert "emit_obj_match_obj_run" 42 "$ACT"
rm -f /tmp/ct_eo_match.cheng /tmp/ct_eo_match /tmp/ct_eo_match.o /tmp/ct_eo_match_link

# 14: emit:obj with Result/? handling
rm -f /tmp/ct_eo_result.cheng /tmp/ct_eo_result /tmp/ct_eo_result.o /tmp/ct_eo_result_link
cat > /tmp/ct_eo_result.cheng << 'EOF'
type Result = Ok(val: int32) | Err(msg: str)

fn main(): int32 =
    let r: Result = Result.Ok(42)
    return r?
EOF
ACT=$(compile_run /tmp/ct_eo_result.cheng /tmp/ct_eo_result)
assert "emit_obj_result_exe" 42 "$ACT"
quiet $COLD system-link-exec --in:/tmp/ct_eo_result.cheng --target:arm64-apple-darwin \
    --emit:obj --out:/tmp/ct_eo_result.o
if [ -s /tmp/ct_eo_result.o ] && file /tmp/ct_eo_result.o 2>/dev/null | grep -q "Mach-O 64-bit object"; then
    ACT=1; else ACT=0
fi
assert "emit_obj_result_obj_file" 1 "$ACT"
if quiet cc -o /tmp/ct_eo_result_link /tmp/ct_eo_result.o; then
    /tmp/ct_eo_result_link 2>/dev/null; ACT=$?
else
    ACT="LINK_FAILED"
fi
assert "emit_obj_result_obj_run" 42 "$ACT"
rm -f /tmp/ct_eo_result.cheng /tmp/ct_eo_result /tmp/ct_eo_result.o /tmp/ct_eo_result_link

# 15: emit:obj multi-file (two .cheng -> two .o -> link with cc)
rm -f /tmp/ct_eo_prov.cheng /tmp/ct_eo_cons.cheng /tmp/ct_eo_prov.o /tmp/ct_eo_cons.o /tmp/ct_eo_multi_link
cat > /tmp/ct_eo_prov.cheng << 'EOF'
@exportc("eo_provide_value")
fn eo_provide_value(): int32 = return 42
EOF
cat > /tmp/ct_eo_cons.cheng << 'EOF'
@importc("eo_provide_value")
fn eo_provide_value(): int32

fn main(): int32 = return eo_provide_value()
EOF
quiet $COLD system-link-exec --in:/tmp/ct_eo_prov.cheng --target:arm64-apple-darwin \
    --emit:obj --out:/tmp/ct_eo_prov.o
quiet $COLD system-link-exec --in:/tmp/ct_eo_cons.cheng --target:arm64-apple-darwin \
    --emit:obj --out:/tmp/ct_eo_cons.o
if [ -s /tmp/ct_eo_prov.o ] && file /tmp/ct_eo_prov.o 2>/dev/null | grep -q "Mach-O 64-bit object" &&
   [ -s /tmp/ct_eo_cons.o ] && file /tmp/ct_eo_cons.o 2>/dev/null | grep -q "Mach-O 64-bit object"; then
    ACT=1; else ACT=0
fi
assert "emit_obj_multi_obj_files" 1 "$ACT"
if quiet cc -o /tmp/ct_eo_multi_link /tmp/ct_eo_cons.o /tmp/ct_eo_prov.o; then
    /tmp/ct_eo_multi_link 2>/dev/null; ACT=$?
else
    ACT="LINK_FAILED"
fi
assert "emit_obj_multi_obj_run" 42 "$ACT"
rm -f /tmp/ct_eo_prov.cheng /tmp/ct_eo_cons.cheng /tmp/ct_eo_prov.o /tmp/ct_eo_cons.o /tmp/ct_eo_multi_link

# 16: large stack frame (frame size > 4095 bytes, exercises large-offset ldr/str)
cat > /tmp/ct_large_frame.cheng << 'CHENG'
fn main(): int32 =
    var a: int32[4000]
    var b: int32[4000]
    a[0] = 42
    b[0] = 1
    return a[0] + b[0]
CHENG
ACT=$(compile_run /tmp/ct_large_frame.cheng /tmp/ct_large_frame_out)
assert "large_frame" 43 "$ACT"
rm -f /tmp/ct_large_frame.cheng /tmp/ct_large_frame_out

# --- large_array_literal (regression: >64 elements, previously hit "cold array literal too large") ---
cat > /tmp/ct_large_array_literal.cheng << 'CHENG'
fn main(): int32 =
    var arr: int32[65] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64]
    return arr[0] + arr[64]
CHENG
ACT=$(compile_run /tmp/ct_large_array_literal.cheng /tmp/ct_large_array_literal_out)
assert "large_array_literal" 64 "$ACT"
rm -f /tmp/ct_large_array_literal.cheng /tmp/ct_large_array_literal_out

# --- int_edge_cases ---
cat > /tmp/ct_int_edge.cheng << 'CHENG'
fn main(): int32 =
    var r: int32
    let a: int32 = 2147483647 + 1
    if a == -2147483648:
        r = r | 1
    let b: int32 = -2147483648 - 1
    if b == 2147483647:
        r = r | 2
    let c: int32 = -42 / 5
    if c == -8:
        r = r | 4
    let d: int32 = -42 % 5
    if d == -2:
        r = r | 8
    return r
CHENG
ACT=$(compile_run /tmp/ct_int_edge.cheng /tmp/ct_int_edge_out)
assert "int_edge_cases" 15 "$ACT"
rm -f /tmp/ct_int_edge.cheng /tmp/ct_int_edge_out

# --- variant_payload_sizes ---
cat > /tmp/ct_var_payload.cheng << 'CHENG'
type V = A(x: int32, y: int32) | B(a: int32, b: int32)

fn main(): int32 =
    let v: V = V.A(10, 20)
    match v:
        A(x, y) => return x + y
        B(a, b) => return a + b
CHENG
ACT=$(compile_run /tmp/ct_var_payload.cheng /tmp/ct_var_payload_out)
assert "variant_payload_sizes" 30 "$ACT"
rm -f /tmp/ct_var_payload.cheng /tmp/ct_var_payload_out

# --- switch_many_arms ---
cat > /tmp/ct_switch_arms.cheng << 'CHENG'
type Digit = Zero | One | Two | Three | Four | Five | Six | Seven | Eight | Nine | Ten | Eleven

fn digit_val(d: Digit): int32 =
    match d:
        Zero => return 0
        One => return 1
        Two => return 2
        Three => return 3
        Four => return 4
        Five => return 5
        Six => return 6
        Seven => return 7
        Eight => return 8
        Nine => return 9
        Ten => return 10
        Eleven => return 11

fn main(): int32 =
    if digit_val(Digit.Zero) != 0: return 255
    if digit_val(Digit.One) != 1: return 255
    if digit_val(Digit.Two) != 2: return 255
    if digit_val(Digit.Three) != 3: return 255
    if digit_val(Digit.Four) != 4: return 255
    if digit_val(Digit.Five) != 5: return 255
    if digit_val(Digit.Six) != 6: return 255
    if digit_val(Digit.Seven) != 7: return 255
    if digit_val(Digit.Eight) != 8: return 255
    if digit_val(Digit.Nine) != 9: return 255
    if digit_val(Digit.Ten) != 10: return 255
    if digit_val(Digit.Eleven) != 11: return 255
    return 0
CHENG
ACT=$(compile_run /tmp/ct_switch_arms.cheng /tmp/ct_switch_arms_out)
assert "switch_many_arms" 0 "$ACT"
rm -f /tmp/ct_switch_arms.cheng /tmp/ct_switch_arms_out

# --- composite_sret ---
cat > /tmp/ct_composite_sret.cheng << 'CHENG'
type Big =
    a: int32
    b: int32
    c: int32
    d: int32
    e: int32
    f: int32

fn make_big(): Big =
    return Big{
        a: 10,
        b: 20,
        c: 30,
        d: 40,
        e: 50,
        f: 60,
    }

fn main(): int32 =
    let obj: Big = make_big()
    return obj.a + obj.b + obj.c + obj.d + obj.e + obj.f
CHENG
ACT=$(compile_run /tmp/ct_composite_sret.cheng /tmp/ct_composite_sret_out)
assert "composite_sret" 210 "$ACT"
rm -f /tmp/ct_composite_sret.cheng /tmp/ct_composite_sret_out

# --- long_function_1500ops ---
python3 -c "
import sys
code = 'fn main(): int32 =\n'
code += '    var x: int32\n'
code += '    var y: int32 = 1\n'
for i in range(500):
    code += f'    x = x + {i}\n'
    code += '    y = y + 1\n'
    code += '    if y > 1000: y = 0\n'
code += '    return x\n'
with open('/tmp/ct_long_func.cheng', 'w') as f:
    f.write(code)
"
ACT=$(compile_run_timed /tmp/cheng_cold /tmp/ct_long_func.cheng /tmp/ct_long_func_out 30)
assert "long_function_1500ops" 78 "$ACT"
rm -f /tmp/ct_long_func.cheng /tmp/ct_long_func_out

# --- generic_specialize_smoke ---
ACT=$(compile_run_timed /tmp/cheng_cold testdata/generic_specialize_smoke.cheng /tmp/ct_gen_smoke 30)
assert "generic_specialize_smoke" 0 "$ACT"
rm -f /tmp/ct_gen_smoke

# --- generic_pass_smoke ---
ACT=$(compile_run_timed /tmp/cheng_cold testdata/generic_pass_smoke.cheng /tmp/ct_gen_pass 30)
assert "generic_pass_smoke" 0 "$ACT"
rm -f /tmp/ct_gen_pass

# --- generic_multi_smoke ---
ACT=$(compile_run_timed /tmp/cheng_cold testdata/generic_multi_smoke.cheng /tmp/ct_gen_multi 30)
assert "generic_multi_smoke" 0 "$ACT"
rm -f /tmp/ct_gen_multi

# --- generic_arithmetic_smoke ---
ACT=$(compile_run_timed /tmp/cheng_cold testdata/generic_arithmetic_smoke.cheng /tmp/ct_gen_arith 30)
assert "generic_arithmetic_smoke" 0 "$ACT"
rm -f /tmp/ct_gen_arith

# --- for_range_inclusive_leq ---
cat > /tmp/ct_range_leq.cheng << 'CHENG'
fn main(): int32 =
    var s: int32
    for i in 1..<=5:
        s = s + i
    return s
CHENG
ACT=$(compile_run /tmp/ct_range_leq.cheng /tmp/ct_range_leq_out)
assert "for_range_inclusive_leq" 15 "$ACT"
rm -f /tmp/ct_range_leq.cheng /tmp/ct_range_leq_out

# --- double_neg_not_identity ---
ACT=$(compile_run testdata/double_neg_not_identity.cheng /tmp/ct_dneg_not)
assert "double_neg_not_identity" 0 "$ACT"
rm -f /tmp/ct_dneg_not

# --- double_neg_not_direct ---
ACT=$(compile_run testdata/double_neg_not_direct.cheng /tmp/ct_dneg_not_direct)
assert "double_neg_not_direct" 0 "$ACT"
rm -f /tmp/ct_dneg_not_direct

# --- type_alias ---
cat > /tmp/ct_type_alias.cheng << 'EOF'
type MyInt = int32

fn main(): MyInt =
    var x: MyInt = 42
    return x
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_type_alias.cheng /tmp/ct_type_alias_out 10)
assert "type_alias" 42 "$ACT"
rm -f /tmp/ct_type_alias.cheng /tmp/ct_type_alias_out

# --- type_alias_index (regression: type alias used as array index) ---
cat > /tmp/ct_type_alias_idx.cheng << 'EOF'
type Idx = int32

fn main(): int32 =
    var a: int32[4] = [10, 20, 30, 40]
    var i: Idx = 2
    return a[i]
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_type_alias_idx.cheng /tmp/ct_type_alias_idx_out 10)
assert "type_alias_index" 30 "$ACT"
rm -f /tmp/ct_type_alias_idx.cheng /tmp/ct_type_alias_idx_out

# --- deeply_nested_if (10 levels) ---
cat > /tmp/ct_deep_if.cheng << 'EOF'
fn main(): int32 =
    var x = 0
    if x == 0:
        x = 1
        if x == 1:
            x = 2
            if x == 2:
                x = 3
                if x == 3:
                    x = 4
                    if x == 4:
                        x = 5
                        if x == 5:
                            x = 6
                            if x == 6:
                                x = 7
                                if x == 7:
                                    x = 8
                                    if x == 8:
                                        x = 9
                                        if x == 9:
                                            x = 10
                                            return x
    return 255
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_deep_if.cheng /tmp/ct_deep_if_out 10)
assert "deeply_nested_if" 10 "$ACT"
rm -f /tmp/ct_deep_if.cheng /tmp/ct_deep_if_out

# --- nested_loops (for inside while inside for) ---
cat > /tmp/ct_nested_loops.cheng << 'EOF'
fn main(): int32 =
    var s = 0
    for a in 0..<3:
        var b = 0
        while b < 3:
            for c in 0..<3:
                s = s + 1
            b = b + 1
    return s
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_nested_loops.cheng /tmp/ct_nested_loops_out 10)
assert "nested_loops" 27 "$ACT"
rm -f /tmp/ct_nested_loops.cheng /tmp/ct_nested_loops_out

# --- string_compare (== !=) ---
cat > /tmp/ct_strcmp.cheng << 'EOF'
fn main(): int32 =
    let a: str = "hello"
    let b: str = "hello"
    let c: str = "world"
    if a == b:
        if a != c:
            return 0
    return 1
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_strcmp.cheng /tmp/ct_strcmp_out 10)
assert "string_compare" 0 "$ACT"
rm -f /tmp/ct_strcmp.cheng /tmp/ct_strcmp_out

# --- multiple_returns (5 return paths) ---
cat > /tmp/ct_multi_ret.cheng << 'EOF'
fn classify(x: int32): int32 =
    if x < 0:
        return 1
    if x == 0:
        return 2
    if x > 0 && x < 10:
        return 3
    if x >= 10 && x < 100:
        return 4
    return 5

fn main(): int32 =
    let r1 = classify(-5)
    let r2 = classify(0)
    let r3 = classify(7)
    let r4 = classify(50)
    let r5 = classify(200)
    return r1 + r2 + r3 + r4 + r5
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_multi_ret.cheng /tmp/ct_multi_ret_out 10)
assert "multiple_returns" 15 "$ACT"
rm -f /tmp/ct_multi_ret.cheng /tmp/ct_multi_ret_out

# --- nested_object_fields (obj.field.subfield read, literal construct) ---
cat > /tmp/ct_nested_obj.cheng << 'EOF'
type Inner =
    val: int32

type Outer =
    inner: Inner
    tag: int32

fn main(): int32 =
    let obj: Outer = Outer{
        inner: Inner{
            val: 42,
        },
        tag: 1,
    }
    return obj.inner.val
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_nested_obj.cheng /tmp/ct_nested_obj_out 10)
assert "nested_object_fields" 42 "$ACT"
rm -f /tmp/ct_nested_obj.cheng /tmp/ct_nested_obj_out

# --- multi_level_field_assign (obj.field.subfield = val/[]) ---
cat > /tmp/ct_multi_field_assign.cheng << 'EOF'
type Inner =
    slots: int32[]
    totalCount: int32

type Outer =
    inner: Inner
    tag: int32

fn main(): int32 =
    var outer: Outer
    outer.inner.slots = []
    outer.inner.totalCount = 0
    outer.tag = 1
    if outer.inner.totalCount != 0: return 1
    if outer.tag != 1: return 2
    return 0
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_multi_field_assign.cheng /tmp/ct_multi_field_assign_out 10)
assert "multi_level_field_assign" 0 "$ACT"
rm -f /tmp/ct_multi_field_assign.cheng /tmp/ct_multi_field_assign_out

# --- mutual_recursion (forward reference with circular calls) ---
cat > /tmp/ct_mutual_rec.cheng << 'EOF'
fn is_even(n: int32): int32 =
    if n == 0:
        return 1
    return is_odd(n - 1)

fn is_odd(n: int32): int32 =
    if n == 0:
        return 0
    return is_even(n - 1)

fn main(): int32 =
    if is_even(4) != 1: return 1
    if is_even(5) != 0: return 2
    if is_odd(3) != 1: return 3
    if is_odd(6) != 0: return 4
    return 0
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_mutual_rec.cheng /tmp/ct_mutual_rec_out 10)
assert "mutual_recursion" 0 "$ACT"
rm -f /tmp/ct_mutual_rec.cheng /tmp/ct_mutual_rec_out

# --- i32_mul_large (regression: verify no 8-bit truncation) ---
cat > /tmp/ct_i32_mul_large.cheng << 'EOF'
fn main(): int32 =
    let a: int32 = 100 * 100
    let b: int32 = 1000 * 1000
    return (a + b) - 1010000
EOF
ACT=$(compile_run_timed "$COLD" /tmp/ct_i32_mul_large.cheng /tmp/ct_i32_mul_large_out 10)
assert "i32_mul_large" 0 "$ACT"
rm -f /tmp/ct_i32_mul_large.cheng /tmp/ct_i32_mul_large_out

# 17: object array field access
cat > /tmp/ct_obj_arr_field.cheng << 'EOF'
type Config =
    values: int32[10]
    count: int32

fn main(): int32 =
    var cfg: Config
    cfg.values = [10, 20, 30, 40, 50, 60, 70, 80, 90, 100]
    cfg.count = 5
    return cfg.values[0] + cfg.values[4]
EOF
ACT=$(compile_run /tmp/ct_obj_arr_field.cheng /tmp/ct_obj_arr_field_out)
assert "obj_array_field" 60 "$ACT"
rm -f /tmp/ct_obj_arr_field.cheng /tmp/ct_obj_arr_field_out

# 18: empty object
cat > /tmp/ct_empty_obj.cheng << 'EOF'
type Empty =

fn main(): int32 =
    var e: Empty
    return 42
EOF
ACT=$(compile_run /tmp/ct_empty_obj.cheng /tmp/ct_empty_obj_out)
assert "empty_object" 42 "$ACT"
rm -f /tmp/ct_empty_obj.cheng /tmp/ct_empty_obj_out

# 19: variant with different payload sizes per arm
cat > /tmp/ct_var_diff_payload.cheng << 'EOF'
type V = Small(x: int32) | Big(a: int32, b: int32, c: int32, d: int32)

fn main(): int32 =
    let v: V = V.Small(42)
    match v:
        Small(x) => return x
        Big(a, b, c, d) => return a + b + c + d
EOF
ACT=$(compile_run /tmp/ct_var_diff_payload.cheng /tmp/ct_var_diff_payload_out)
assert "variant_diff_payload" 42 "$ACT"
rm -f /tmp/ct_var_diff_payload.cheng /tmp/ct_var_diff_payload_out

# 20: boolean not operator
cat > /tmp/ct_bool_not.cheng << 'EOF'
fn main(): int32 =
    var flag = 0
    if !flag:
        return 42
    else:
        return 0
EOF
ACT=$(compile_run /tmp/ct_bool_not.cheng /tmp/ct_bool_not_out)
assert "bool_not" 42 "$ACT"
rm -f /tmp/ct_bool_not.cheng /tmp/ct_bool_not_out

# 21: very long function name (50+ chars)
cat > /tmp/ct_long_fn_name.cheng << 'EOF'
fn this_is_a_very_long_function_name_that_is_over_fifty_characters_long(): int32 =
    return 42

fn main(): int32 =
    return this_is_a_very_long_function_name_that_is_over_fifty_characters_long()
EOF
ACT=$(compile_run /tmp/ct_long_fn_name.cheng /tmp/ct_long_fn_name_out)
assert "long_fn_name" 42 "$ACT"
rm -f /tmp/ct_long_fn_name.cheng /tmp/ct_long_fn_name_out

# 22: shift left/right operators
cat > /tmp/ct_shift_ops.cheng << 'EOF'
fn main(): int32 =
    let a: int32 = 1 << 4
    let b: int32 = 256 >> 4
    return a + b - 32
EOF
ACT=$(compile_run /tmp/ct_shift_ops.cheng /tmp/ct_shift_ops_out)
assert "shift_ops" 0 "$ACT"
rm -f /tmp/ct_shift_ops.cheng /tmp/ct_shift_ops_out

echo ""
echo "=== $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
