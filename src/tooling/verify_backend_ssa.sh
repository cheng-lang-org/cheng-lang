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


out_dir="artifacts/backend_ssa"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_while_break.cheng"
if [ ! -f "$fixture" ]; then
  echo "[Error] missing fixture: $fixture" 1>&2
  exit 2
fi

run_generic_mode() {
  mode="$1"
  budget="$2"
  outfile="$3"
  env $link_env \
    GENERIC_MODE="$mode" \
    GENERIC_SPEC_BUDGET="$budget" \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$outfile" \
    "$driver"
}

exe_a="$out_dir/dict_mode"
run_generic_mode dict 0 "$exe_a"

exe_b="$out_dir/hybrid_mode"
run_generic_mode hybrid 0 "$exe_b"

if [ ! -x "$exe_a" ] || [ ! -x "$exe_b" ]; then
  echo "verify_backend_ssa: compile failed" 1>&2
  exit 1
fi

"$exe_a"
"$exe_b"

if [ "$("$exe_a")" != "$("$exe_b")" ]; then
  echo "verify_backend_ssa: dict/hybrid outputs differ" 1>&2
  exit 1
fi

echo "verify_backend_ssa ok"
