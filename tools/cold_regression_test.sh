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

ACT=$(compile_run src/tests/import_use.cheng /tmp/ct_iu)
assert "import_use" 3 "$ACT"

quiet $COLD system-link-exec --in:src/core/tooling/backend_driver_dispatch_min.cheng \
    --target:arm64-apple-darwin --out:/tmp/ct_dm
if [ -x /tmp/ct_dm ]; then
    STATUS=$(/tmp/ct_dm status --root:. --in:src/core/tooling/backend_driver_dispatch_min.cheng --out:/dev/null 2>/dev/null)
    HAS_EDGES=$(echo "$STATUS" | grep -c 'flag_exec_edges=0' || true)
    assert "dispatch_min" 1 "$HAS_EDGES"
else
    assert "dispatch_min" 0 "COMPILE_FAILED"
fi

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
