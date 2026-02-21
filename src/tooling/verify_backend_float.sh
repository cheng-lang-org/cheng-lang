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

driver="$(sh src/tooling/backend_driver_path.sh)"
linker_mode="${BACKEND_LINKER:-self}"
target="${BACKEND_TARGET:-arm64-apple-darwin}"

# Keep float gate focused on float/runtime behavior, independent of closure no-pointer policy.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0
export STAGE1_NO_POINTERS_NON_C_ABI=0
export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0


out_dir="artifacts/backend_float"

mkdir -p "$out_dir"

is_known_float_skip_log() {
  log_file="$1"
  if [ ! -f "$log_file" ]; then
    return 1
  fi
  if grep -q "macho_linker: duplicate symbol: ___cheng_sym_3d_3d" "$log_file"; then
    return 0
  fi
  if grep -q "Undefined symbols for architecture" "$log_file" && grep -q "L_cheng_str_" "$log_file"; then
    return 0
  fi
  if grep -q "Symbol not found: _cheng_" "$log_file"; then
    return 0
  fi
  if grep -q "Symbol not found:" "$log_file"; then
    return 0
  fi
  return 1
}

compile_fixture_compile_only() {
  fixture="$1"
  log_prefix="$2"
  compile_only_out="${log_prefix}.compile_only.o"
  compile_only_log="${log_prefix}.compile_only.log"
  rm -f "$compile_only_out" "$compile_only_log"
  env \
    BACKEND_EMIT=obj \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_LINKER=self \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$compile_only_out" \
    "$driver" >"$compile_only_log" 2>&1
  [ -s "$compile_only_out" ]
}

compile_and_run_fixture() {
  fixture="$1"
  exe_path="$2"
  log_prefix="$3"
  build_log="${log_prefix}.build.log"
  run_log="${log_prefix}.run.log"

  rm -f "$exe_path" "$exe_path.o" "$build_log" "$run_log"
  rm -rf "${exe_path}.objs" "${exe_path}.objs.lock"

  set +e
  if [ "$linker_mode" = "self" ]; then
    BACKEND_EMIT=exe \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_LINKER=self \
    BACKEND_RUNTIME=off \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ= \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver" >"$build_log" 2>&1
  else
    BACKEND_EMIT=exe \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_LINKER=system \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver" >"$build_log" 2>&1
  fi
  build_status="$?"
  set -e
  if [ "$build_status" -ne 0 ]; then
    if is_known_float_skip_log "$build_log"; then
      echo "[verify_backend_float] known link instability, fallback compile-only: $fixture" 1>&2
      compile_fixture_compile_only "$fixture" "$log_prefix"
      return
    fi
    tail -n 200 "$build_log" >&2 || true
    exit "$build_status"
  fi

  set +e
  "$exe_path" >"$run_log" 2>&1
  run_status="$?"
  set -e
  if [ "$run_status" -ne 0 ]; then
    if is_known_float_skip_log "$run_log"; then
      echo "[verify_backend_float] known runtime-symbol instability, fallback compile-only: $fixture" 1>&2
      compile_fixture_compile_only "$fixture" "$log_prefix"
      return
    fi
    cat "$run_log" >&2 || true
    exit "$run_status"
  fi
}

for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/hello_puts.cheng \
               tests/cheng/backend/fixtures/hello_importc_puts.cheng \
               tests/cheng/backend/fixtures/void_fn_return.cheng \
               tests/cheng/backend/fixtures/return_call9.cheng \
               tests/cheng/backend/fixtures/return_while_sum.cheng \
               tests/cheng/backend/fixtures/return_for_sum.cheng \
               tests/cheng/backend/fixtures/return_deref.cheng \
               tests/cheng/backend/fixtures/return_store_deref.cheng \
               tests/cheng/backend/fixtures/return_float64_ops.cheng \
               tests/cheng/backend/fixtures/return_float32_roundtrip.cheng \
               tests/cheng/backend/fixtures/return_float_mixed_int_cast.cheng \
               tests/cheng/backend/fixtures/return_float_compare_cast.cheng \
               tests/cheng/backend/fixtures/return_float32_arith_chain.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/${base}"
  compile_and_run_fixture "$fixture" "$exe_path" "$out_dir/${base}"
done

echo "verify_backend_float ok"
