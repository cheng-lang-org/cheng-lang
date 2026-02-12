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


out_dir="artifacts/backend_multi_lto"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"
exe_path="$out_dir/return_add_lto"

env $link_env \
  CHENG_BACKEND_OPT2=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_MULTI=1 \
  CHENG_BACKEND_MULTI_LTO=1 \
  CHENG_BACKEND_JOBS=4 \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"

"$exe_path"

count="0"
if [ -d "$exe_path.objs" ]; then
  count="$(find "$exe_path.objs" -name '*.o' | wc -l | tr -d ' ')"
elif [ -f "$exe_path.o" ]; then
  # Some backends materialize the post-LTO object as a single sidecar `.o`.
  count="1"
fi
test "$count" -eq 1

echo "verify_backend_multi_lto ok"
