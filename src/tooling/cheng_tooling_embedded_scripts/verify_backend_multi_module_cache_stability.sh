#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
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

timeout_cmd=""
if command -v timeout >/dev/null 2>&1; then
  timeout_cmd="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
  timeout_cmd="gtimeout"
fi

gate_timeout="${BACKEND_MULTI_MODULE_CACHE_STABILITY_TIMEOUT:-60}"
driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
target="${BACKEND_TARGET:-$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target)}"
fixture="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi

out_dir="artifacts/backend_multi_module_cache_stability"
mkdir -p "$out_dir"
module_cache_path="$out_dir/module.cache.tsv"

run_case() {
  label="$1"
  mm_cache="$2"
  unstable_allow="$3"
  out_path="$out_dir/${label}.exe"
  log_path="$out_dir/${label}.log"
  rm -f "$out_path" "$out_path.o" "$log_path"
  rm -rf "${out_path}.objs" "${out_path}.objs.lock"
  set +e
  if [ "$timeout_cmd" != "" ] && [ "$gate_timeout" -gt 0 ] 2>/dev/null; then
    "$timeout_cmd" "${gate_timeout}s" env \
      BACKEND_PROFILE=1 \
      BACKEND_EMIT=exe \
      BACKEND_LINKER=system \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_TARGET="$target" \
      BACKEND_MULTI=1 \
      BACKEND_MULTI_FORCE=1 \
      BACKEND_JOBS=4 \
      BACKEND_MULTI_MODULE_CACHE="$mm_cache" \
      BACKEND_MODULE_CACHE_UNSTABLE_ALLOW="$unstable_allow" \
      BACKEND_MODULE_CACHE="$module_cache_path" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_path" \
      "$driver" >"$log_path" 2>&1
    rc="$?"
  else
    env \
      BACKEND_PROFILE=1 \
      BACKEND_EMIT=exe \
      BACKEND_LINKER=system \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_TARGET="$target" \
      BACKEND_MULTI=1 \
      BACKEND_MULTI_FORCE=1 \
      BACKEND_JOBS=4 \
      BACKEND_MULTI_MODULE_CACHE="$mm_cache" \
      BACKEND_MODULE_CACHE_UNSTABLE_ALLOW="$unstable_allow" \
      BACKEND_MODULE_CACHE="$module_cache_path" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_path" \
      "$driver" >"$log_path" 2>&1
    rc="$?"
  fi
  set -e

  if [ "$rc" -eq 124 ]; then
    echo "[verify_backend_multi_module_cache_stability] timeout (${gate_timeout}s): $label" 1>&2
    tail -n 120 "$log_path" 1>&2 || true
    exit 1
  fi
  if [ "$rc" -eq 139 ]; then
    echo "[verify_backend_multi_module_cache_stability] segv detected: $label" 1>&2
    tail -n 120 "$log_path" 1>&2 || true
    exit 1
  fi
  if [ "$rc" -ne 0 ]; then
    echo "[verify_backend_multi_module_cache_stability] build failed: $label (rc=$rc)" 1>&2
    tail -n 120 "$log_path" 1>&2 || true
    exit "$rc"
  fi
  if [ ! -f "$out_path" ]; then
    echo "[verify_backend_multi_module_cache_stability] missing output: $out_path" 1>&2
    tail -n 120 "$log_path" 1>&2 || true
    exit 1
  fi
  echo "[verify_backend_multi_module_cache_stability] $label ok"
}

run_case "default_off" "0" "0"
run_case "unstable_override_first" "1" "1"
run_case "unstable_override_second" "1" "1"

echo "verify_backend_multi_module_cache_stability ok"
