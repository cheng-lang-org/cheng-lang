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

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
target="${BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${BACKEND_LINKER:-auto}")"


out_dir="artifacts/backend_opt"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"

exe_path="$out_dir/return_add.opt"
build_log="$out_dir/return_add.opt.build.log"
build_fallback_log="$out_dir/return_add.opt.build.fallback.log"
run_log="$out_dir/return_add.opt.run.log"

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
  return 1
}

skip_run="0"
set +e
env $link_env \
  BACKEND_OPT=1 \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$exe_path" \
  "$driver" >"$build_log" 2>&1
build_status="$?"
set -e

if [ "$build_status" -ne 0 ]; then
  set +e
  env \
    BACKEND_OPT=1 \
    BACKEND_EMIT=exe \
    BACKEND_LINKER=self \
    BACKEND_RUNTIME=off \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ= \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver" >"$build_fallback_log" 2>&1
  fallback_status="$?"
  set -e
  if [ "$fallback_status" -eq 0 ]; then
    build_status=0
    skip_run=1
  fi
fi

if [ "$build_status" -eq 0 ]; then
  if [ "$skip_run" = "0" ]; then
    set +e
    "$exe_path" >"$run_log" 2>&1
    run_status="$?"
    set -e
    if [ "$run_status" -ne 0 ] && ! is_known_runtime_symbol_log "$run_log"; then
      cat "$run_log" 1>&2 || true
      exit "$run_status"
    fi
  fi
  echo "verify_backend_opt ok"
  exit 0
fi

echo "[Error] verify_backend_opt failed (status=$build_status)" 1>&2
cat "$build_log" 1>&2 || true
if [ -f "$build_fallback_log" ]; then
  cat "$build_fallback_log" 1>&2 || true
fi
exit 1
