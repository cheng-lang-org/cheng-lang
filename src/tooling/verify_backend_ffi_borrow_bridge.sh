#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

fail() {
  echo "[verify_backend_ffi_borrow_bridge] $1" >&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

driver="${BACKEND_DRIVER:-$(sh src/tooling/backend_driver_path.sh)}"
if [ ! -x "$driver" ]; then
  fail "backend driver not executable: $driver"
fi
driver_exec="$driver"
driver_real_env=""
if [ -x "src/tooling/backend_driver_exec.sh" ]; then
  driver_exec="src/tooling/backend_driver_exec.sh"
  driver_real_env="BACKEND_DRIVER_REAL=$driver"
fi

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
gate_linker="${BACKEND_FFI_BORROW_BRIDGE_LINKER:-system}"
case "$gate_linker" in
  ""|auto|system)
    link_env="BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=0"
    gate_linker="system"
    ;;
  self)
    link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:self)"
    ;;
  *)
    fail "invalid BACKEND_FFI_BORROW_BRIDGE_LINKER: $gate_linker (expected self|system|auto)"
    ;;
esac

fixture_ok="tests/cheng/backend/fixtures/ffi_importc_borrow_mut_pair_i32.cheng"
fixture_fail="tests/cheng/backend/fixtures/compile_fail_ffi_importc_borrow_mut_pair_i32_non_lvalue.cheng"
for f in "$fixture_ok" "$fixture_fail"; do
  if [ ! -f "$f" ]; then
    fail "missing fixture: $f"
  fi
done

out_dir="artifacts/backend_ffi_borrow_bridge"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
exe_path="$out_dir/ffi_importc_borrow_mut_pair_i32.$safe_target"
case "$target" in
  *windows*|*msvc*) exe_path="$exe_path.exe" ;;
esac
build_ok_log="$out_dir/ffi_borrow_bridge.$safe_target.build_ok.log"
run_log="$out_dir/ffi_borrow_bridge.$safe_target.run.log"
build_fail_log="$out_dir/ffi_borrow_bridge.$safe_target.build_fail.log"
symbol_log="$out_dir/ffi_borrow_bridge.$safe_target.symbol.log"
report="$out_dir/backend_ffi_borrow_bridge.$safe_target.report.txt"
snapshot="$out_dir/backend_ffi_borrow_bridge.$safe_target.snapshot.env"

rm -f "$exe_path" "$exe_path.o" "$build_ok_log" "$run_log" "$build_fail_log" "$symbol_log" "$report" "$snapshot"
rm -rf "${exe_path}.objs" "${exe_path}.objs.lock"

set +e
env $link_env \
  $driver_real_env \
  BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_ok" \
  BACKEND_OUTPUT="$exe_path" \
  "$driver_exec" >"$build_ok_log" 2>&1
build_ok_status="$?"
set -e
if [ "$build_ok_status" -ne 0 ]; then
  sed -n '1,200p' "$build_ok_log" >&2 || true
  fail "positive fixture build failed (status=$build_ok_status)"
fi
if [ ! -x "$exe_path" ]; then
  fail "missing executable output: $exe_path"
fi

set +e
"$exe_path" >"$run_log" 2>&1
run_status="$?"
set -e
if [ "$run_status" -ne 0 ]; then
  sed -n '1,120p' "$run_log" >&2 || true
  fail "positive fixture run failed (status=$run_status)"
fi

symbol_pattern='_cheng_abi_borrow_mut_pair_i32'
symbol_ok="0"
set +e
nm -m "$exe_path" >"$symbol_log" 2>/dev/null
nm_status="$?"
if [ "$nm_status" -ne 0 ]; then
  nm -g "$exe_path" >"$symbol_log" 2>/dev/null
  nm_status="$?"
fi
set -e
if [ "$nm_status" -ne 0 ]; then
  fail "failed to inspect executable symbols via nm"
fi
if rg -q "$symbol_pattern" "$symbol_log"; then
  symbol_ok="1"
else
  sed -n '1,200p' "$symbol_log" >&2 || true
  fail "missing expected borrow bridge symbol: $symbol_pattern"
fi

fail_out="$out_dir/compile_fail_ffi_importc_borrow_mut_pair_i32_non_lvalue.$safe_target"
set +e
env $link_env \
  $driver_real_env \
  BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_fail" \
  BACKEND_OUTPUT="$fail_out" \
  "$driver_exec" >"$build_fail_log" 2>&1
build_fail_status="$?"
set -e
diag_pattern='var parameter requires a mutable borrow \(lvalue\)'
diag_supported="0"
diag_mode="not_supported_in_backend_driver"
if rg -q "$diag_pattern" "$build_fail_log"; then
  diag_supported="1"
  diag_mode="sem_diag"
fi

{
  echo "verify_backend_ffi_borrow_bridge report"
  echo "target=$target"
  echo "gate_linker=$gate_linker"
  echo "link_env=$link_env"
  echo "driver=$driver"
  echo "driver_exec=$driver_exec"
  echo "fixture_ok=$fixture_ok"
  echo "fixture_fail=$fixture_fail"
  echo "build_ok_status=$build_ok_status"
  echo "run_status=$run_status"
  echo "symbol_pattern=$symbol_pattern"
  echo "symbol_ok=$symbol_ok"
  echo "build_fail_status=$build_fail_status"
  echo "diag_pattern=$diag_pattern"
  echo "diag_supported=$diag_supported"
  echo "diag_mode=$diag_mode"
  echo "build_ok_log=$build_ok_log"
  echo "run_log=$run_log"
  echo "symbol_log=$symbol_log"
  echo "build_fail_log=$build_fail_log"
} >"$report"

{
  echo "backend_ffi_borrow_bridge_target=$target"
  echo "backend_ffi_borrow_bridge_symbol_ok=$symbol_ok"
  echo "backend_ffi_borrow_bridge_diag_supported=$diag_supported"
  echo "backend_ffi_borrow_bridge_diag_mode=$diag_mode"
  echo "backend_ffi_borrow_bridge_report=$report"
} >"$snapshot"

echo "verify_backend_ffi_borrow_bridge ok"
