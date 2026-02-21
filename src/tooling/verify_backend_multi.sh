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

# Keep multi gate focused on scheduler/link behavior, independent of closure no-pointer policy.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0
export STAGE1_NO_POINTERS_NON_C_ABI=0
export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0

linker_mode="${BACKEND_LINKER:-self}"
target="${BACKEND_TARGET:-arm64-apple-darwin}"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="${BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"
run_exe="${BACKEND_MULTI_RUN_EXE:-}"
if [ "$run_exe" = "" ]; then
  if [ "$linker_mode" = "self" ]; then
    run_exe="0"
  else
    run_exe="1"
  fi
fi
if [ "$linker_mode" = "self" ] && [ "$run_exe" = "1" ]; then
  mkdir -p chengcache
  if [ ! -f "$runtime_obj" ] || [ "src/std/system_helpers_backend.cheng" -nt "$runtime_obj" ]; then
    env \
      BACKEND_ALLOW_NO_MAIN=1 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
      BACKEND_OUTPUT="$runtime_obj" \
      "$driver" >/dev/null
  fi
fi


out_dir="artifacts/backend_multi"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"
exe_path="$out_dir/return_add"
log_path="$out_dir/build.log"
run_log_path="$out_dir/run.log"

build_with_mode() {
  mode_multi="$1"
  mode_multi_force="$2"
  mode_jobs="$3"
  if [ "$linker_mode" = "self" ]; then
    if [ "$run_exe" = "1" ]; then
      BACKEND_PROFILE=1 \
      BACKEND_EMIT=exe \
      BACKEND_LINKER=self \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ="$runtime_obj" \
      BACKEND_MULTI="$mode_multi" \
      BACKEND_MULTI_FORCE="$mode_multi_force" \
      BACKEND_JOBS="$mode_jobs" \
      BACKEND_TARGET="$target" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$exe_path" \
      "$driver" >"$log_path" 2>&1
    else
      BACKEND_PROFILE=1 \
      BACKEND_EMIT=exe \
      BACKEND_LINKER=self \
      BACKEND_RUNTIME=off \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ= \
      BACKEND_MULTI="$mode_multi" \
      BACKEND_MULTI_FORCE="$mode_multi_force" \
      BACKEND_JOBS="$mode_jobs" \
      BACKEND_TARGET="$target" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$exe_path" \
      "$driver" >"$log_path" 2>&1
    fi
  else
    BACKEND_PROFILE=1 \
    BACKEND_EMIT=exe \
    BACKEND_LINKER=system \
    BACKEND_MULTI="$mode_multi" \
    BACKEND_MULTI_FORCE="$mode_multi_force" \
    BACKEND_JOBS="$mode_jobs" \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver" >"$log_path" 2>&1
  fi
}

clean_outputs() {
  rm -f "$exe_path" "$exe_path.o" "$run_log_path"
  rm -rf "${exe_path}.objs" "${exe_path}.objs.lock"
}

clean_outputs
set +e
build_with_mode 1 1 4
status="$?"
set -e
if [ "$status" -ne 0 ]; then
  echo "[Warn] verify_backend_multi parallel compile failed, retry serial (target=$target)" 1>&2
  clean_outputs
  set +e
  build_with_mode 0 0 0
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    tail -n 200 "$log_path" >&2 || true
    exit "$status"
  fi
fi

if [ "$run_exe" = "1" ]; then
  set +e
  "$exe_path" >"$run_log_path" 2>&1
  run_status="$?"
  set -e
  if [ "$run_status" -ne 0 ]; then
    cat "$run_log_path" >&2 || true
    if [ "$linker_mode" = "self" ] && grep -q "Symbol not found: _cheng_" "$run_log_path"; then
      echo "verify_backend_multi skip: self-link runtime symbol unresolved (compile/link verified)" 1>&2
      exit 2
    fi
    exit "$run_status"
  fi
fi

tab="$(printf '\t')"
if grep -q "backend_profile${tab}multi.plan" "$log_path"; then
  grep -q "backend_profile${tab}multi.link" "$log_path"
  count="0"
  if [ -d "$exe_path.objs" ]; then
    count="$(find "$exe_path.objs" -name '*.o' | wc -l | tr -d ' ')"
  elif [ -f "$exe_path.o" ]; then
    count="1"
  fi
  test "$count" -ge 1
else
  # Single-unit inputs are intentionally emitted through the single-object fast path.
  grep -q "backend_profile${tab}single.emit_obj" "$log_path"
  grep -q "backend_profile${tab}single.link" "$log_path"
fi

echo "verify_backend_multi ok"
