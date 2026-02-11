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


n="${CHENG_BACKEND_STRESS_N:-10}"
out_dir="artifacts/backend_stress"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
exe_path="$out_dir/stage1_smoke"

env $link_env \
  CHENG_C_SYSTEM=system \
  CHENG_MM=orc \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"

i=0
while [ "$i" -lt "$n" ]; do
  "$exe_path" | grep -Fq "hello from cheng backend"
  i=$((i + 1))
done

echo "verify_backend_stress ok (n=$n)"
