#!/bin/bash
# Cold compiler regression test suite
# Run: bash tools/cold_regression_test.sh
set -uo pipefail

COLD="${CHENG_COLD:-/tmp/cheng_cold}"
[ -x "$COLD" ] || { echo "Build: cc -std=c11 -O2 -o /tmp/cheng_cold bootstrap/cheng_cold.c"; exit 1; }

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

# 1-3: Core tests
ACT=$(compile_run src/tests/ordinary_zero_exit_fixture.cheng /tmp/ct_oz)
assert "ordinary_zero" 0 "$ACT"

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

rm -f /tmp/ct_bad_import
quiet $COLD system-link-exec --in:src/tests/cold_bad_import_unresolved_main.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_bad_import
if [ -x /tmp/ct_bad_import ]; then
    ACT="UNEXPECTED_SUCCESS"
else
    ACT="COMPILE_FAILED"
fi
assert "import_unresolved_hard_fail" "COMPILE_FAILED" "$ACT"

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

# 7: language subset coverage
ACT=$(compile_run src/tests/cold_subset_coverage.cheng /tmp/ct_cov)
assert "subset_coverage" 0 "$ACT"

echo ""
echo "=== $PASS passed, $FAIL failed ==="
[ "$FAIL" -eq 0 ]
