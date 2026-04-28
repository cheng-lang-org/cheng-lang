#!/bin/bash
# cfg_regression_gate.sh
# Regression gate for the general CFG codegen pipeline.
# Compiles and runs all existing minimal fixtures, asserting exit codes.
#
# Usage:
#   cd /root/cheng-lang
#   bash src/tests/cfg_regression_gate.sh
#
# Each fixture is compiled via system-link-exec, run, and its
# exit code checked against the expected value.

set -euo pipefail

CHENG="${CHENG:-artifacts/backend_driver/cheng}"
ROOT="${ROOT:-/root/cheng-lang}"
TARGET="${TARGET:-arm64-apple-darwin}"
TMPDIR="${TMPDIR:-/tmp/cfg_regression_gate}"
PASS=0
FAIL=0

mkdir -p "$TMPDIR"

log()    { printf "  [gate] %s\n" "$1"; }
pass()   { printf "  PASS   %s\n" "$1"; PASS=$((PASS + 1)); }
fail()   { printf "  FAIL   %s (exit %d, expected %d)\n" "$1" "$2" "$3"; FAIL=$((FAIL + 1)); }
check()  {
    local name=$1; shift
    local expected=$1; shift
    local out="$TMPDIR/$name"
    local report="$out.report.txt"
    local runlog="$out.run.log"

    log "compiling $name ..."
    if "$CHENG" system-link-exec \
        --root:"$ROOT" \
        --in:"$ROOT/src/tests/${name}.cheng" \
        --emit:exe \
        --target:"$TARGET" \
        --out:"$out" \
        --report-out:"$report" 2>"$out.compile.err"; then
        :
    else
        local rc=$?
        printf "  COMPILE_ERR %s (exit %d)\n" "$name" "$rc"
        cat "$out.compile.err" | head -5
        FAIL=$((FAIL + 1))
        return
    fi

    log "running $name ..."
    if "$out" >"$runlog" 2>&1; then
        local actual=$?
    else
        local actual=$?
    fi

    if [ "$actual" -eq "$expected" ]; then
        pass "$name"
    else
        fail "$name" "$actual" "$expected"
    fi
}

echo "============================================"
echo " CFG Regression Gate"
echo "============================================"
echo "compiler: $CHENG"
echo "root:     $ROOT"
echo "target:   $TARGET"
echo "tmpdir:   $TMPDIR"
echo ""

# ---- Fixtures ----

# 1. ordinary_zero_exit_fixture
#    fn main(): int32 = return 0  -> exit 0
check "ordinary_zero_exit_fixture" 0

# 2. int32_const_return_direct_fixture
#    fn ReturnSeven(): int32 = 7; fn main(): int32 = ReturnSeven()  -> exit 7
check "int32_const_return_direct_fixture" 7

# 3. float64_mul_backend_smoke
#    fn MulF64(a: float64, b: float64): float64 = return a * b
#    fn main(): int32 = ... 1.5 * 2.0 == 3.0 ? exit 0 : exit 1  -> exit 0
check "float64_mul_backend_smoke" 0

# 4. function_task_contract_smoke
#    Pure-data function task types: plan/task/result/merge contract smoke  -> exit 0
check "function_task_contract_smoke" 0

# 5. cfg_body_ir_contract_smoke
#    BodyIR construction and field access contract  -> exit 0
check "cfg_body_ir_contract_smoke" 0

# 6. cfg_lowering_smoke
#    let/if/return guard function  -> exit 0
check "cfg_lowering_smoke" 0

# 7. cfg_multi_stmt_smoke
#    Multi-statement let/call chain  -> exit 0
check "cfg_multi_stmt_smoke" 0

# 8. cfg_result_project_smoke
#    Result[T] projection with IsErr/Value  -> exit 0
check "cfg_result_project_smoke" 0

echo ""
echo "============================================"
echo " Results: $PASS passed, $FAIL failed"
echo "============================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
exit 0
