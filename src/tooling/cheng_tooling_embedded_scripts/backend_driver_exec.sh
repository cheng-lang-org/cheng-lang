#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

driver_is_script_shim() {
  bin="$1"
  [ -f "$bin" ] || return 1
  first_line="$(sed -n '1p' "$bin" 2>/dev/null || true)"
  case "$first_line" in
    '#!'*) return 0 ;;
  esac
  return 1
}

driver_current_contract_ok() {
  bin="$1"
  [ -x "$bin" ] || return 1
  if driver_is_script_shim "$bin"; then
    return 0
  fi
  if ! command -v strings >/dev/null 2>&1; then
    return 0
  fi
  if ! command -v rg >/dev/null 2>&1; then
    return 0
  fi
  set +e
  strings "$bin" 2>/dev/null | rg -q '^BACKEND_OUTPUT$'
  has_backend_output="$?"
  strings "$bin" 2>/dev/null | rg -q '^CHENG_BACKEND_OUTPUT$'
  has_legacy_output="$?"
  strings "$bin" 2>/dev/null | rg -q 'backend_driver: output path required'
  has_output_msg="$?"
  set -e
  if [ "$has_output_msg" -eq 0 ] && [ "$has_legacy_output" -eq 0 ] && [ "$has_backend_output" -ne 0 ]; then
    return 1
  fi
  return 0
}

resolve_real_driver() {
  if [ "${BACKEND_DRIVER_REAL:-}" != "" ]; then
    if [ -x "${BACKEND_DRIVER_REAL}" ]; then
      real_candidate="${BACKEND_DRIVER_REAL}"
      if driver_current_contract_ok "$real_candidate"; then
        printf '%s\n' "$real_candidate"
        return 0
      fi
      echo "[backend_driver_exec] BACKEND_DRIVER_REAL uses rejected legacy driver contract: ${BACKEND_DRIVER_REAL}" >&2
      return 1
    fi
    echo "[backend_driver_exec] BACKEND_DRIVER_REAL is not runnable: ${BACKEND_DRIVER_REAL}" >&2
    return 1
  fi

  for cand in \
    "$root/artifacts/v3_backend_driver/cheng"; do
    if [ -x "$cand" ] && driver_current_contract_ok "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

cleanup_sidecar() {
  emit_mode="${BACKEND_EMIT:-}"
  output_path="${BACKEND_OUTPUT:-}"
  keep_obj="${BACKEND_KEEP_EXE_OBJ:-0}"
  case "$emit_mode" in
    exe)
      ;;
    *)
      return 0
      ;;
  esac
  case "$output_path" in
    ""|-)
      return 0
      ;;
  esac
  case "$keep_obj" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
  esac
  rm -f "${output_path}.o" "${output_path}.tmp.linkobj" "${output_path}.tmp"
  rm -rf "${output_path}.objs" "${output_path}.objs.lock"
}

real_driver="$(resolve_real_driver)" || {
  echo "[backend_driver_exec] no healthy backend driver candidate found" >&2
  exit 1
}

if [ -x "$root/src/tooling/backend_driver_exec.sh" ]; then
  set +e
  BACKEND_DRIVER_REAL="$real_driver" "$root/src/tooling/backend_driver_exec.sh" "$@"
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    cleanup_sidecar
  fi
  exit "$status"
fi

set +e
"$real_driver" "$@"
status="$?"
set -e
if [ "$status" -eq 0 ]; then
  cleanup_sidecar
fi
exit "$status"
