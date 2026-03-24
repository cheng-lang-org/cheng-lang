#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
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

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
requested_linker="${BACKEND_LINKER:-self}"

resolve_target() {
  if [ "${BACKEND_TARGET:-}" != "" ] && [ "${BACKEND_TARGET:-}" != "auto" ]; then
    printf '%s\n' "${BACKEND_TARGET}"
    return
  fi
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target
}

resolve_link_env() {
  link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:"$requested_linker")"
  resolved_linker=""
  resolved_no_runtime_c="0"
  resolved_runtime_obj=""
  resolved_runtime_obj_assigned="0"
  for entry in $link_env; do
    case "$entry" in
      BACKEND_LINKER=*)
        resolved_linker="${entry#BACKEND_LINKER=}"
        ;;
      BACKEND_NO_RUNTIME_C=*)
        resolved_no_runtime_c="${entry#BACKEND_NO_RUNTIME_C=}"
        ;;
      BACKEND_RUNTIME_OBJ=*)
        resolved_runtime_obj="${entry#BACKEND_RUNTIME_OBJ=}"
        resolved_runtime_obj_assigned="1"
        ;;
    esac
  done
  if [ "$resolved_linker" = "" ]; then
    echo "[Error] verify_backend_float: backend_link_env missing BACKEND_LINKER" 1>&2
    exit 1
  fi
}

# Keep float gate focused on float/runtime behavior, independent of closure no-pointer policy.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0
export STAGE1_NO_POINTERS_NON_C_ABI=0
export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0


out_dir="artifacts/backend_float"

mkdir -p "$out_dir"

compile_and_run_fixture() {
  fixture="$1"
  exe_path="$2"
  log_prefix="$3"
  build_log="${log_prefix}.build.log"
  run_log="${log_prefix}.run.log"

  rm -f "$exe_path" "$exe_path.o" "$build_log" "$run_log"
  rm -rf "${exe_path}.objs" "${exe_path}.objs.lock"

  set +e
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --no-multi \
      --no-multi-force \
      --no-runtime-c \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$exe_path" >"$build_log" 2>&1
  elif [ "$resolved_runtime_obj_assigned" = "1" ]; then
    "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --no-multi \
      --no-multi-force \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$exe_path" >"$build_log" 2>&1
  elif [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --no-multi \
      --no-multi-force \
      --no-runtime-c \
      --output:"$exe_path" >"$build_log" 2>&1
  else
    "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --no-multi \
      --no-multi-force \
      --output:"$exe_path" >"$build_log" 2>&1
  fi
  build_status="$?"
  set -e
  if [ "$build_status" -ne 0 ]; then
    tail -n 200 "$build_log" >&2 || true
    exit "$build_status"
  fi

  set +e
  "$exe_path" >"$run_log" 2>&1
  run_status="$?"
  set -e
  if [ "$run_status" -ne 0 ]; then
    cat "$run_log" >&2 || true
    exit "$run_status"
  fi
}

target="$(resolve_target)"
resolve_link_env

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
