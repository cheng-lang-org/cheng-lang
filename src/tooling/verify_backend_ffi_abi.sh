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
target="${BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${BACKEND_LINKER:-auto}")"


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
  env $link_env \
    BACKEND_EMIT=exe \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver"
  "$exe_path"
done

echo "verify_backend_ffi_abi ok"
