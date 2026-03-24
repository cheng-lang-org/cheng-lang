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
requested_linker="${BACKEND_LINKER:-auto}"

resolve_target() {
  if [ "${BACKEND_TARGET:-}" != "" ] && [ "${BACKEND_TARGET:-}" != "auto" ]; then
    printf '%s\n' "${BACKEND_TARGET}"
    return
  fi
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target 2>/dev/null || printf '%s\n' arm64-apple-darwin
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
    echo "[Error] verify_backend_ffi_abi: backend_link_env missing BACKEND_LINKER" 1>&2
    exit 1
  fi
}

run_fixture() {
  fixture="$1"
  exe_path="$2"
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --no-multi \
      --no-multi-force \
      --no-runtime-c \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$exe_path"
    return
  fi
  if [ "$resolved_runtime_obj_assigned" = "1" ]; then
    "$driver" "$fixture" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --no-multi \
      --no-multi-force \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$exe_path"
    return
  fi
  if [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --no-multi \
      --no-multi-force \
      --no-runtime-c \
      --output:"$exe_path"
    return
  fi
  "$driver" "$fixture" \
    --frontend:stage1 \
    --emit:exe \
    --target:"$target" \
    --linker:"$resolved_linker" \
    --no-multi \
    --no-multi-force \
    --output:"$exe_path"
}

target="$(resolve_target)"
resolve_link_env


out_dir="artifacts/backend_ffi_abi"
mkdir -p "$out_dir"

for fixture in tests/cheng/backend/fixtures/ffi_importc_sum9_i64.cheng \
               tests/cheng/backend/fixtures/ffi_importc_sum9_i32.cheng \
               tests/cheng/backend/fixtures/ffi_importc_varargs_sum10_i32.cheng \
               tests/cheng/backend/fixtures/ffi_importc_varargs_direct_sum_i32.cheng \
               tests/cheng/backend/fixtures/ffi_importc_sum16_i64.cheng \
               tests/cheng/backend/fixtures/ffi_importc_sum16_i32.cheng \
               tests/cheng/backend/fixtures/ffi_importc_mix_i32_i64.cheng \
               tests/cheng/backend/fixtures/ffi_importc_ptr_store_load_i32.cheng \
               tests/cheng/backend/fixtures/ffi_importc_callback_ctx_i32.cheng \
               tests/cheng/backend/fixtures/ffi_importc_aggret_pair_i32.cheng \
               tests/cheng/backend/fixtures/ffi_importc_out_pair_i32.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/$base"
  run_fixture "$fixture" "$exe_path"
  "$exe_path"
done

echo "verify_backend_ffi_abi ok"
