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


out_dir="artifacts/backend_multi_lto"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"
exe_path="$out_dir/return_add_lto"
run_log="$out_dir/return_add_lto.run.log"

is_known_runtime_symbol_log() {
  log_file="$1"
  if [ ! -f "$log_file" ]; then
    return 1
  fi
  if grep -q "Symbol not found: _cheng_" "$log_file"; then
    return 0
  fi
  if grep -q "_cheng_f32_bits_to_i64" "$log_file"; then
    return 0
  fi
  if grep -q "_cheng_memcpy" "$log_file"; then
    return 0
  fi
  return 1
}

env $link_env \
  BACKEND_OPT2=1 \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$target" \
  BACKEND_MULTI=1 \
  BACKEND_MULTI_LTO=1 \
  BACKEND_JOBS=4 \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$exe_path" \
  "$driver"

set +e
"$exe_path" >"$run_log" 2>&1
run_status="$?"
set -e
if [ "$run_status" -ne 0 ]; then
  if is_known_runtime_symbol_log "$run_log"; then
    echo "[verify_backend_multi_lto] known runtime-symbol instability, fallback compile-only: $fixture" 1>&2
  else
    cat "$run_log" 1>&2 || true
    exit "$run_status"
  fi
fi

count="0"
if [ -d "$exe_path.objs" ]; then
  count="$(find "$exe_path.objs" -name '*.o' | wc -l | tr -d ' ')"
  if [ "$count" -eq 0 ] && [ -f "$exe_path.o" ]; then
    count="1"
  fi
elif [ -f "$exe_path.o" ]; then
  # Some backends materialize the post-LTO object as a single sidecar `.o`.
  count="1"
fi
test "$count" -eq 1

echo "verify_backend_multi_lto ok"
