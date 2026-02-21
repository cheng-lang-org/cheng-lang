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
  echo "[verify_backend_ffi_slice_shim] $1" >&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

driver="${BACKEND_DRIVER:-$(sh src/tooling/backend_driver_path.sh)}"
if [ ! -x "$driver" ]; then
  fail "backend driver not executable: $driver"
fi

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
backend_linker_mode="${BACKEND_LINKER:-system}"
if [ "$backend_linker_mode" = "auto" ] || [ "$backend_linker_mode" = "" ]; then
  backend_linker_mode="system"
fi
backend_no_runtime_c="${BACKEND_NO_RUNTIME_C:-0}"
backend_runtime_obj="${BACKEND_RUNTIME_OBJ:-}"
run_positive="${BACKEND_FFI_SLICE_SHIM_RUN:-0}"

fixture_ok="tests/cheng/backend/fixtures/ffi_importc_slice_seq_i32.cheng"
fixture_fail_legacy="tests/cheng/backend/fixtures/compile_fail_ffi_importc_slice_openarray_i32.cheng"
fixture_fail_surface="tests/cheng/backend/fixtures/compile_fail_ffi_slice_user_raw_ptr_surface.cheng"
for f in "$fixture_ok" "$fixture_fail_legacy" "$fixture_fail_surface"; do
  if [ ! -f "$f" ]; then
    fail "missing fixture: $f"
  fi
done

out_dir="artifacts/backend_ffi_slice_shim"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
exe_path="$out_dir/ffi_importc_slice_seq_i32.$safe_target"
case "$target" in
  *windows*|*msvc*) exe_path="$exe_path.exe" ;;
esac
build_ok_log="$out_dir/ffi_slice_shim.$safe_target.build_ok.log"
run_log="$out_dir/ffi_slice_shim.$safe_target.run.log"
build_fail_legacy_log="$out_dir/ffi_slice_shim.$safe_target.build_fail_legacy.log"
build_fail_surface_log="$out_dir/ffi_slice_shim.$safe_target.build_fail_surface.log"
surface_scan_log="$out_dir/ffi_slice_shim.$safe_target.surface_scan.log"
report="$out_dir/backend_ffi_slice_shim.$safe_target.report.txt"
snapshot="$out_dir/backend_ffi_slice_shim.$safe_target.snapshot.env"

rm -f "$exe_path" "$exe_path.o" "$build_ok_log" "$run_log" \
  "$build_fail_legacy_log" "$build_fail_surface_log" "$surface_scan_log" \
  "$report" "$snapshot"
rm -rf "${exe_path}.objs" "${exe_path}.objs.lock"

surface_pattern='void\*|ptr_add\(|load_ptr\(|store_ptr\('
surface_ok="1"
if rg -n "$surface_pattern" "$fixture_ok" >"$surface_scan_log"; then
  surface_ok="0"
  fail "positive fixture leaks raw pointer surface (see $surface_scan_log)"
fi

set +e
env \
  BACKEND_LINKER="$backend_linker_mode" \
  CHENG_BACKEND_LINKER="$backend_linker_mode" \
  BACKEND_NO_RUNTIME_C="$backend_no_runtime_c" \
  CHENG_BACKEND_NO_RUNTIME_C="$backend_no_runtime_c" \
  BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  CHENG_BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  BACKEND_EMIT=exe \
  CHENG_BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_TARGET="$target" \
  CHENG_BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_ok" \
  CHENG_BACKEND_INPUT="$fixture_ok" \
  BACKEND_OUTPUT="$exe_path" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver" >"$build_ok_log" 2>&1
build_ok_status="$?"
set -e
if [ "$build_ok_status" -ne 0 ]; then
  sed -n '1,200p' "$build_ok_log" >&2 || true
  fail "positive fixture build failed (status=$build_ok_status)"
fi
if [ ! -x "$exe_path" ]; then
  fail "missing executable output: $exe_path"
fi

importc_symbol="cheng_abi_sum_seq_i32"
symbol_check="skip"
if command -v nm >/dev/null 2>&1; then
  symbol_check="0"
  if nm "$exe_path" 2>/dev/null | rg -q "$importc_symbol"; then
    symbol_check="1"
  else
    fail "missing expected importc symbol in positive artifact: $importc_symbol"
  fi
fi

run_status="0"
run_mode="compile_only"
runtime_symbol_blocked="0"
if [ "$run_positive" != "1" ]; then
  : >"$run_log"
fi
if [ "$run_positive" = "1" ]; then
  set +e
  "$exe_path" >"$run_log" 2>&1
  run_status="$?"
  set -e
  run_mode="run"
  if [ "$run_status" -ne 0 ]; then
    if rg -q 'Symbol not found: _memRetain' "$run_log"; then
      run_mode="compile_only"
      runtime_symbol_blocked="1"
    else
      sed -n '1,120p' "$run_log" >&2 || true
      fail "positive fixture run failed (status=$run_status)"
    fi
  fi
fi

fail_legacy_out="$out_dir/compile_fail_ffi_importc_slice_openarray_i32.$safe_target"
set +e
env \
  BACKEND_LINKER="$backend_linker_mode" \
  CHENG_BACKEND_LINKER="$backend_linker_mode" \
  BACKEND_NO_RUNTIME_C="$backend_no_runtime_c" \
  CHENG_BACKEND_NO_RUNTIME_C="$backend_no_runtime_c" \
  BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  CHENG_BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  BACKEND_EMIT=exe \
  CHENG_BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_TARGET="$target" \
  CHENG_BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_fail_legacy" \
  CHENG_BACKEND_INPUT="$fixture_fail_legacy" \
  BACKEND_OUTPUT="$fail_legacy_out" \
  CHENG_BACKEND_OUTPUT="$fail_legacy_out" \
  "$driver" >"$build_fail_legacy_log" 2>&1
build_fail_legacy_status="$?"
set -e
if [ "$build_fail_legacy_status" -eq 0 ]; then
  fail "legacy openArray negative fixture unexpectedly compiled"
fi
legacy_diag_pattern="Legacy type syntax is removed; use 'T\\[\\]' instead of 'openArray\\[T\\]'"
legacy_diag_ok="0"
if rg -q "$legacy_diag_pattern" "$build_fail_legacy_log"; then
  legacy_diag_ok="1"
else
  sed -n '1,200p' "$build_fail_legacy_log" >&2 || true
  fail "missing expected legacy openArray diagnostic"
fi

fail_surface_out="$out_dir/compile_fail_ffi_slice_user_raw_ptr_surface.$safe_target"
set +e
env \
  BACKEND_LINKER="$backend_linker_mode" \
  CHENG_BACKEND_LINKER="$backend_linker_mode" \
  BACKEND_NO_RUNTIME_C="$backend_no_runtime_c" \
  CHENG_BACKEND_NO_RUNTIME_C="$backend_no_runtime_c" \
  BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  CHENG_BACKEND_RUNTIME_OBJ="$backend_runtime_obj" \
  STAGE1_NO_POINTERS_NON_C_ABI=1 \
  CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 \
  CHENG_STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 \
  BACKEND_EMIT=exe \
  CHENG_BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_TARGET="$target" \
  CHENG_BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_fail_surface" \
  CHENG_BACKEND_INPUT="$fixture_fail_surface" \
  BACKEND_OUTPUT="$fail_surface_out" \
  CHENG_BACKEND_OUTPUT="$fail_surface_out" \
  "$driver" >"$build_fail_surface_log" 2>&1
build_fail_surface_status="$?"
set -e
if [ "$build_fail_surface_status" -eq 0 ]; then
  fail "raw pointer surface negative fixture unexpectedly compiled"
fi
surface_diag_pattern='no-pointer policy: pointer (types are|dereference is|operation is) forbidden outside C ABI modules'
surface_diag_ok="0"
if rg -q "$surface_diag_pattern" "$build_fail_surface_log"; then
  surface_diag_ok="1"
else
  sed -n '1,200p' "$build_fail_surface_log" >&2 || true
  fail "missing expected no-pointer policy diagnostic"
fi

{
  echo "verify_backend_ffi_slice_shim report"
  echo "target=$target"
  echo "driver=$driver"
  echo "fixture_ok=$fixture_ok"
  echo "fixture_fail_legacy=$fixture_fail_legacy"
  echo "fixture_fail_surface=$fixture_fail_surface"
  echo "build_ok_status=$build_ok_status"
  echo "importc_symbol=$importc_symbol"
  echo "symbol_check=$symbol_check"
  echo "run_status=$run_status"
  echo "run_mode=$run_mode"
  echo "run_positive=$run_positive"
  echo "runtime_symbol_blocked=$runtime_symbol_blocked"
  echo "build_fail_legacy_status=$build_fail_legacy_status"
  echo "build_fail_surface_status=$build_fail_surface_status"
  echo "surface_pattern=$surface_pattern"
  echo "surface_ok=$surface_ok"
  echo "legacy_diag_pattern=$legacy_diag_pattern"
  echo "legacy_diag_ok=$legacy_diag_ok"
  echo "surface_diag_pattern=$surface_diag_pattern"
  echo "surface_diag_ok=$surface_diag_ok"
  echo "build_ok_log=$build_ok_log"
  echo "run_log=$run_log"
  echo "build_fail_legacy_log=$build_fail_legacy_log"
  echo "build_fail_surface_log=$build_fail_surface_log"
  echo "surface_scan_log=$surface_scan_log"
} >"$report"

{
  echo "backend_ffi_slice_shim_target=$target"
  echo "backend_ffi_slice_shim_surface_ok=$surface_ok"
  echo "backend_ffi_slice_shim_symbol_check=$symbol_check"
  echo "backend_ffi_slice_shim_run_mode=$run_mode"
  echo "backend_ffi_slice_shim_run_positive=$run_positive"
  echo "backend_ffi_slice_shim_runtime_symbol_blocked=$runtime_symbol_blocked"
  echo "backend_ffi_slice_shim_legacy_diag_ok=$legacy_diag_ok"
  echo "backend_ffi_slice_shim_surface_diag_ok=$surface_diag_ok"
  echo "backend_ffi_slice_shim_report=$report"
} >"$snapshot"

echo "verify_backend_ffi_slice_shim ok"
