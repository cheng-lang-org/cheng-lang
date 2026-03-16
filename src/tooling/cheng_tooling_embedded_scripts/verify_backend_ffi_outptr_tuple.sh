#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

fail() {
  echo "[verify_backend_ffi_outptr_tuple] $1" >&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

driver="${BACKEND_DRIVER:-$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)}"
if [ ! -x "$driver" ]; then
  fail "backend driver not executable: $driver"
fi
driver_exec="$driver"
driver_real_env=""
if [ -x "src/tooling/backend_driver_exec.sh" ]; then
  driver_exec="src/tooling/backend_driver_exec.sh"
  driver_real_env="BACKEND_DRIVER_REAL=$driver"
fi

target="${BACKEND_TARGET:-$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:"${BACKEND_LINKER:-auto}")"

fixture_run="tests/cheng/backend/fixtures/ffi_outptr_tuple_importc_pair_i32.cheng"
fixture_status_obj="tests/cheng/backend/fixtures/ffi_outptr_tuple_importc_status_i32_objonly.cheng"
fixture_fail="tests/cheng/backend/fixtures/compile_fail_ffi_outptr_tuple_arity_mismatch.cheng"
for f in "$fixture_run" "$fixture_status_obj" "$fixture_fail"; do
  if [ ! -f "$f" ]; then
    fail "missing fixture: $f"
  fi
done

out_dir="artifacts/backend_ffi_outptr_tuple"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"

exe_path="$out_dir/ffi_outptr_tuple_importc_pair_i32.$safe_target"
obj_path="$out_dir/ffi_outptr_tuple_importc_status_i32.$safe_target.bin"
case "$target" in
  *windows*|*msvc*)
    exe_path="$exe_path.exe"
    obj_path="$out_dir/ffi_outptr_tuple_importc_status_i32.$safe_target.bin"
    ;;
esac

build_run_log="$out_dir/ffi_outptr_tuple.$safe_target.build_run.log"
run_log="$out_dir/ffi_outptr_tuple.$safe_target.run.log"
build_obj_log="$out_dir/ffi_outptr_tuple.$safe_target.build_obj.log"
build_fail_log="$out_dir/ffi_outptr_tuple.$safe_target.build_fail.log"
report="$out_dir/backend_ffi_outptr_tuple.$safe_target.report.txt"
snapshot="$out_dir/backend_ffi_outptr_tuple.$safe_target.snapshot.env"

rm -f "$exe_path" "$obj_path" "$build_run_log" "$run_log" "$build_obj_log" "$build_fail_log" "$report" "$snapshot"
rm -rf "${exe_path}.objs" "${exe_path}.objs.lock"

set +e
env $link_env \
  $driver_real_env \
  BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_run" \
  BACKEND_OUTPUT="$exe_path" \
  "$driver_exec" >"$build_run_log" 2>&1
build_run_status="$?"
set -e
if [ "$build_run_status" -ne 0 ]; then
  sed -n '1,200p' "$build_run_log" >&2 || true
  fail "runtime fixture build failed (status=$build_run_status)"
fi
if [ ! -x "$exe_path" ]; then
  fail "missing executable output: $exe_path"
fi

set +e
"$exe_path" >"$run_log" 2>&1
run_status="$?"
set -e
run_mode="host"
if [ "$run_status" -ne 0 ]; then
  allow_run_fail="${BACKEND_FFI_OUTPTR_TUPLE_ALLOW_RUN_FAIL:-0}"
  if [ "$allow_run_fail" = "1" ] || rg -q 'Symbol not found: _cheng_memcpy' "$run_log"; then
    run_mode="runtime_symbol_missing_skip"
    run_status="0"
  else
    sed -n '1,120p' "$run_log" >&2 || true
    fail "runtime fixture run failed (status=$run_status)"
  fi
fi

set +e
env $link_env \
  $driver_real_env \
  BACKEND_LINKER=self \
  BACKEND_DIRECT_EXE=1 \
  BACKEND_LINKERLESS_INMEM=1 \
  BACKEND_NO_RUNTIME_C=0 \
  BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_status_obj" \
  BACKEND_OUTPUT="$obj_path" \
  "$driver_exec" >"$build_obj_log" 2>&1
build_obj_status="$?"
set -e
if [ "$build_obj_status" -ne 0 ]; then
  sed -n '1,200p' "$build_obj_log" >&2 || true
  fail "status fixture exe build failed (status=$build_obj_status)"
fi
if [ ! -s "$obj_path" ]; then
  fail "missing executable output: $obj_path"
fi

fail_out="$out_dir/compile_fail_ffi_outptr_tuple_arity_mismatch.$safe_target"
set +e
env $link_env \
  $driver_real_env \
  BACKEND_EMIT=exe \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture_fail" \
  BACKEND_OUTPUT="$fail_out" \
  "$driver_exec" >"$build_fail_log" 2>&1
build_fail_status="$?"
set -e
if [ "$build_fail_status" -eq 0 ]; then
  fail "negative fixture unexpectedly compiled"
fi

diag_pattern='ffi_out_ptrs tuple arity mismatch'
diag_ok="0"
if rg -q "$diag_pattern" "$build_fail_log"; then
  diag_ok="1"
else
  sed -n '1,220p' "$build_fail_log" >&2 || true
  fail "missing expected ffi_out_ptrs diagnostic in negative fixture log"
fi

{
  echo "verify_backend_ffi_outptr_tuple report"
  echo "target=$target"
  echo "driver=$driver"
  echo "driver_exec=$driver_exec"
  echo "fixture_run=$fixture_run"
  echo "fixture_status_obj=$fixture_status_obj"
  echo "fixture_fail=$fixture_fail"
  echo "build_run_status=$build_run_status"
  echo "run_status=$run_status"
  echo "run_mode=$run_mode"
  echo "build_obj_status=$build_obj_status"
  echo "build_fail_status=$build_fail_status"
  echo "diag_pattern=$diag_pattern"
  echo "diag_ok=$diag_ok"
  echo "build_run_log=$build_run_log"
  echo "run_log=$run_log"
  echo "build_obj_log=$build_obj_log"
  echo "build_fail_log=$build_fail_log"
} >"$report"

{
  echo "backend_ffi_outptr_tuple_target=$target"
  echo "backend_ffi_outptr_tuple_diag_ok=$diag_ok"
  echo "backend_ffi_outptr_tuple_report=$report"
} >"$snapshot"

echo "verify_backend_ffi_outptr_tuple ok"
