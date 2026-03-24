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

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
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
    echo "[Error] verify_backend_ssa: backend_link_env missing BACKEND_LINKER" 1>&2
    exit 1
  fi
}


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
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --generic-mode:"$mode" \
      --generic-spec-budget:"$budget" \
      --no-runtime-c \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$outfile"
    return
  fi
  if [ "$resolved_runtime_obj_assigned" = "1" ]; then
    "$driver" "$fixture" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --generic-mode:"$mode" \
      --generic-spec-budget:"$budget" \
      --runtime-obj:"$resolved_runtime_obj" \
      --output:"$outfile"
    return
  fi
  if [ "$resolved_no_runtime_c" = "1" ]; then
    "$driver" "$fixture" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --generic-mode:"$mode" \
      --generic-spec-budget:"$budget" \
      --no-runtime-c \
      --output:"$outfile"
    return
  fi
  "$driver" "$fixture" \
    --frontend:stage1 \
    --emit:exe \
    --target:"$target" \
    --linker:"$resolved_linker" \
    --generic-mode:"$mode" \
    --generic-spec-budget:"$budget" \
    --output:"$outfile"
}

target="$(resolve_target)"
resolve_link_env

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
