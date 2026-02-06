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


out_dir="artifacts/backend_opt2"
mkdir -p "$out_dir"

fixture_inline="tests/cheng/backend/fixtures/return_opt2_inline_dce.cheng"

fixture_cse="tests/cheng/backend/fixtures/return_opt2_cse.cheng"

fixture_sroa="tests/cheng/backend/fixtures/return_opt2_sroa_deref.cheng"

fixture_licm="tests/cheng/backend/fixtures/return_opt2_licm_while_cond.cheng"

exe_path="$out_dir/return_opt2_inline_dce.o2"
env $link_env \
  CHENG_BACKEND_OPT2=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture_inline" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"

"$exe_path"

exe_path="$out_dir/return_opt2_cse.o2"
env $link_env \
  CHENG_BACKEND_OPT2=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture_cse" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"

"$exe_path"

exe_path="$out_dir/return_opt2_sroa_deref.o2"
env $link_env \
  CHENG_BACKEND_OPT2=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture_sroa" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"

"$exe_path"

exe_path="$out_dir/return_opt2_licm_while_cond.o2"
env $link_env \
  CHENG_BACKEND_OPT2=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture_licm" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"

"$exe_path"

echo "verify_backend_opt2 ok"
