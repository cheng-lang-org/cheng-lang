#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"
target="${CHENG_BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${CHENG_BACKEND_LINKER:-auto}")"


out_dir="artifacts/backend_ssa"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_while_break.cheng"

exe_a="$out_dir/no_ssa"
env $link_env \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_a" \
  "$driver"

exe_b="$out_dir/with_ssa"
env $link_env \
  CHENG_BACKEND_SSA=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_b" \
  "$driver"

"$exe_a"
"$exe_b"

echo "verify_backend_ssa ok"
