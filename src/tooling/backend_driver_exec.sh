#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

resolve_real_driver() {
  if [ "${BACKEND_DRIVER_REAL:-}" != "" ]; then
    if [ -x "${BACKEND_DRIVER_REAL}" ]; then
      printf '%s\n' "${BACKEND_DRIVER_REAL}"
      return 0
    fi
    echo "[backend_driver_exec] BACKEND_DRIVER_REAL is not runnable: ${BACKEND_DRIVER_REAL}" >&2
    return 1
  fi

  for cand in \
    "$root/artifacts/backend_driver/cheng" \
    "$root/artifacts/backend_seed/cheng.stage2" \
    "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_default" \
    "$root/artifacts/backend_driver/cheng.fixed3" \
    "$root/artifacts/backend_selfhost_self_obj/cheng_stage0_prod" \
    "$root/dist/releases/current/cheng"; do
    if [ -x "$cand" ]; then
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

set +e
"$real_driver" "$@"
status="$?"
set -e
if [ "$status" -eq 0 ]; then
  cleanup_sidecar
fi
exit "$status"
