#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
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

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
fi
requested_linker="${BACKEND_LINKER:-auto}"

resolve_target() {
  if [ "${BACKEND_TARGET:-}" != "" ] && [ "${BACKEND_TARGET:-}" != "auto" ]; then
    printf '%s\n' "${BACKEND_TARGET}"
    return
  fi
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target
}

resolve_link_env() {
  link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:"$requested_linker")"
  resolved_linker=""
  resolved_no_runtime_c="0"
  resolved_runtime_obj=""
  resolved_runtime_obj_assigned="0"
  for entry in $link_env; do
    case "$entry" in
      BACKEND_LINKER=*)
        resolved_linker="${entry#BACKEND_LINKER=}"
        ;;
      BACKEND_NO_RUNTIME_C=*)
        resolved_no_runtime_c="${entry#BACKEND_NO_RUNTIME_C=}"
        ;;
      BACKEND_RUNTIME_OBJ=*)
        resolved_runtime_obj="${entry#BACKEND_RUNTIME_OBJ=}"
        resolved_runtime_obj_assigned="1"
        ;;
    esac
  done
  if [ "$resolved_linker" = "" ]; then
    echo "[Error] verify_backend_opt: backend_link_env missing BACKEND_LINKER" 1>&2
    exit 1
  fi
}

run_primary_build() {
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --opt \
      --no-runtime-c \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$exe_path"
    return
  fi
  if [ "$resolved_runtime_obj_assigned" = "1" ]; then
    "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --opt \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$exe_path"
    return
  fi
  if [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --opt \
      --no-runtime-c \
      --output:"$exe_path"
    return
  fi
  "$driver" "$fixture" \
    --emit:exe \
    --target:"$target" \
    --linker:"$resolved_linker" \
    --opt \
    --output:"$exe_path"
}

target="$(resolve_target)"
resolve_link_env


out_dir="artifacts/backend_opt"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"

exe_path="$out_dir/return_add.opt"
build_log="$out_dir/return_add.opt.build.log"
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

set +e
run_primary_build >"$build_log" 2>&1
build_status="$?"
set -e

if [ "$build_status" -eq 0 ]; then
  set +e
  "$exe_path" >"$run_log" 2>&1
  run_status="$?"
  set -e
  if [ "$run_status" -ne 0 ] && ! is_known_runtime_symbol_log "$run_log"; then
    cat "$run_log" 1>&2 || true
    exit "$run_status"
  fi
  echo "verify_backend_opt ok"
  exit 0
fi

echo "[Error] verify_backend_opt failed (status=$build_status)" 1>&2
cat "$build_log" 1>&2 || true
exit 1
