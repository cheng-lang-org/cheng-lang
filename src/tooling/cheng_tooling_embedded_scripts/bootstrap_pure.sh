#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/bootstrap_pure.sh [--fullspec]

Notes:
  - Pure backend-only bootstrap: C bootstrap path removed.
  - Current-source proof lineage must be published first via
    verify_backend_selfhost_currentsrc_proof.sh.
  - Core path runs verify_backend_selfhost_bootstrap_self_obj.sh.
  - --fullspec additionally runs verify_fullchain_bootstrap.sh (obj-only).

Options:
  --fullspec          Run obj-only fullchain verification after selfhost bootstrap.

Env:
  BOOTSTRAP_UPDATE_SEED=1  Copy artifacts/backend_selfhost_self_obj/cheng.stage2
                                 to artifacts/backend_seed/cheng.stage2.
USAGE
}

fullspec="0"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --fullspec)
      fullspec="1"
      ;;
    --seed:*|--stage0:*)
      echo "[Error] bootstrap-pure no longer accepts stage0 overrides; canonical driver is mandatory" 1>&2
      exit 2
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

bootstrap_script="$root/src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh"
currentsrc_proof_script="$root/src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh"
fullchain_script="$root/src/tooling/cheng_tooling_embedded_scripts/verify_fullchain_bootstrap.sh"
currentsrc_lineage_dir="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof"
currentsrc_stage2="$currentsrc_lineage_dir/cheng.stage2"

if [ ! -x "$currentsrc_stage2" ]; then
  echo "[Error] missing published current-source proof lineage: $currentsrc_stage2" 1>&2
  echo "  run: sh $currentsrc_proof_script" 1>&2
  exit 1
fi

echo "== bootstrap_pure.selfhost_self_obj =="
SELF_OBJ_BOOTSTRAP_CURRENTSRC_LINEAGE_DIR="artifacts/backend_selfhost_self_obj/probe_currentsrc_proof" \
  sh "$bootstrap_script"

if [ "$fullspec" = "1" ]; then
  echo "== bootstrap_pure.fullchain_obj_only =="
  FULLCHAIN_OBJ_ONLY=1 sh "$fullchain_script"
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
