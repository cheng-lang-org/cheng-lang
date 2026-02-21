#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/bootstrap_pure.sh [--fullspec] [--seed:<backend_driver>]

Notes:
  - Pure backend-only bootstrap: C bootstrap path removed.
  - Core path runs verify_backend_selfhost_bootstrap_self_obj.sh.
  - --fullspec additionally runs verify_fullchain_bootstrap.sh (obj-only).

Options:
  --fullspec          Run obj-only fullchain verification after selfhost bootstrap.
  --seed:<path>       Seed backend driver executable path.

Env:
  BOOTSTRAP_UPDATE_SEED=1  Copy artifacts/backend_selfhost_self_obj/cheng.stage2
                                 to artifacts/backend_seed/cheng.stage2.
USAGE
}

fullspec="0"
seed_override=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --fullspec)
      fullspec="1"
      ;;
    --seed:*)
      seed_override="${1#--seed:}"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "$seed_override" != "" ]; then
  export SELF_OBJ_BOOTSTRAP_STAGE0="$seed_override"
fi

echo "== bootstrap_pure.selfhost_self_obj =="
sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh

if [ "$fullspec" = "1" ]; then
  echo "== bootstrap_pure.fullchain_obj_only =="
  FULLCHAIN_OBJ_ONLY=1 sh src/tooling/verify_fullchain_bootstrap.sh
fi

if [ "${BOOTSTRAP_UPDATE_SEED:-0}" = "1" ]; then
  new_seed="artifacts/backend_selfhost_self_obj/cheng.stage2"
  if [ ! -x "$new_seed" ]; then
    echo "[Error] missing refreshed seed stage2: $new_seed" 1>&2
    exit 1
  fi
  mkdir -p artifacts/backend_seed
  cp "$new_seed" "artifacts/backend_seed/cheng.stage2"
  chmod +x "artifacts/backend_seed/cheng.stage2" 2>/dev/null || true
  echo "seed updated: artifacts/backend_seed/cheng.stage2"
fi

echo "bootstrap_pure ok"
