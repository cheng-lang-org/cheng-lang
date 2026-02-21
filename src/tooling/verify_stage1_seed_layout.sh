#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_stage1_seed_layout] $1" 1>&2
  exit 1
}

legacy_seed="src/stage1/frontend_bootstrap.seed.c"
[ ! -f "$legacy_seed" ] || fail "legacy seed must not exist: $legacy_seed"

seed_driver="${SELF_OBJ_BOOTSTRAP_STAGE0:-}"
if [ -z "$seed_driver" ]; then
  for cand in \
    "artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "artifacts/backend_seed/cheng.stage2" \
    "cheng"; do
    if [ -x "$cand" ]; then
      seed_driver="$cand"
      break
    fi
  done
fi

if [ -z "$seed_driver" ]; then
  seed_id=""
  if [ -f "dist/releases/current_id.txt" ]; then
    seed_id="$(cat dist/releases/current_id.txt | tr -d '\r\n')"
  fi
  if [ -n "$seed_id" ]; then
    for tar_path in "dist/releases/$seed_id/backend_release.tar.gz"; do
      if [ -f "$tar_path" ]; then
        seed_driver="$tar_path"
        break
      fi
    done
  fi
fi

[ -n "$seed_driver" ] || fail "missing stage seed driver (set SELF_OBJ_BOOTSTRAP_STAGE0 or prepare artifacts/backend_seed/cheng.stage2)"

case "$seed_driver" in
  *.tar.gz)
    :
    ;;
  *)
    [ -x "$seed_driver" ] || fail "seed driver is not executable: $seed_driver"
    ;;
esac

echo "verify_stage1_seed_layout ok"
