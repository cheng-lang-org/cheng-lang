#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_backend_mvp_driver_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"
target="${CHENG_BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${CHENG_BACKEND_LINKER:-auto}")"


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
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver"
  "$exe_path"
done

echo "verify_backend_ffi_abi ok"
