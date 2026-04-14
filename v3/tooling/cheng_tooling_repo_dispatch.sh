#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
scripts_dir="$root/src/tooling/cheng_tooling_embedded_scripts"

usage() {
  cat <<'EOF'
Usage:
  v3/tooling/cheng_tooling_repo_dispatch.sh <command> [args...]

Supported commands:
  backend_driver_path
  cleanup_cheng_local
  <repo embedded script id>

Notes:
  - This is a minimal repo-local dispatcher used by v3 cross-target smokes.
  - It deliberately avoids the canonical tooling wrapper, so Windows/RISC-V
    gates can run even when `artifacts/tooling_cmd/cheng_tooling` is stale.
EOF
}

driver_help_ok() {
  bin="$1"
  [ -x "$bin" ] || return 1
  set +e
  "$bin" --help >/dev/null 2>&1
  status="$?"
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

pick_driver() {
  for cand in \
    "${BACKEND_DRIVER:-}" \
    "${BACKEND_SELF_LINKER_DRIVER:-}" \
    "${BACKEND_LINKER_ABI_CORE_DRIVER:-}" \
    "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2" \
    "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof" \
    "$root/artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "$root/artifacts/backend_driver/cheng" \
    "$root/dist/releases/current/cheng"; do
    [ "$cand" != "" ] || continue
    if driver_help_ok "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

cmd="${1:-}"
if [ "$cmd" = "" ] || [ "$cmd" = "--help" ] || [ "$cmd" = "-h" ]; then
  usage
  exit 0
fi
shift || true

case "$cmd" in
  backend_driver_path)
    resolved="$(pick_driver || true)"
    if [ "$resolved" = "" ]; then
      echo "[v3 tooling dispatch] no healthy backend driver found" >&2
      exit 1
    fi
    printf '%s\n' "$resolved"
    ;;
  cleanup_cheng_local)
    cleanup_script="$scripts_dir/cleanup_cheng_local.sh"
    if [ -f "$cleanup_script" ]; then
      exec sh "$cleanup_script" "$@"
    fi
    exit 0
    ;;
  *)
    script="$scripts_dir/$cmd.sh"
    if [ ! -f "$script" ]; then
      echo "[v3 tooling dispatch] unsupported command: $cmd" >&2
      exit 2
    fi
    exec sh "$script" "$@"
    ;;
esac
