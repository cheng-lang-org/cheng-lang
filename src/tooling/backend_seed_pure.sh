#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_seed_pure.sh [--seed:<path>] [--out:<path>]

Notes:
  - Rebuilds the backend stage2 driver using a prebuilt seed driver (no C backend).
  - Seed must be an executable backend driver binary (typically a previously published stage2).
  - The seed is copied into chengcache/ before running so it won't be clobbered.
  - Output stage2 is produced at: artifacts/backend_selfhost_self_obj/cheng.stage2

Examples:
  # Use a previously built stage2 as the seed and copy the new seed out:
  sh src/tooling/backend_seed_pure.sh \
    --seed:artifacts/backend_selfhost_self_obj/cheng.stage2 \
    --out:artifacts/backend_seed/cheng.stage2

  # Use a custom seed binary:
  sh src/tooling/backend_seed_pure.sh --seed:/path/to/cheng.stage2
EOF
}

seed=""
out=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --seed:*)
      seed="${1#--seed:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --help|-h)
      usage
      exit 0
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

if [ "$seed" = "" ]; then
  seed="artifacts/backend_selfhost_self_obj/cheng.stage2"
fi

if [ ! -x "$seed" ]; then
  echo "[Error] missing seed driver (expected executable): $seed" 1>&2
  echo "  tip: pass --seed:<path> to a prebuilt stage2 driver" 1>&2
  exit 2
fi

mkdir -p chengcache
seed_copy="chengcache/backend_seed_stage0.$(basename "$seed")"
cp "$seed" "$seed_copy"
chmod +x "$seed_copy" 2>/dev/null || true

echo "== backend.seed_pure.bootstrap =="
SELF_OBJ_BOOTSTRAP_STAGE0="$seed_copy" \
  sh src/tooling/backend_prod_closure.sh --only-self-obj-bootstrap

stage2="artifacts/backend_selfhost_self_obj/cheng.stage2"
if [ ! -x "$stage2" ]; then
  echo "[Error] missing stage2 output: $stage2" 1>&2
  exit 1
fi

stage2_sha=""
if command -v shasum >/dev/null 2>&1; then
  stage2_sha="$(shasum -a 256 "$stage2" | awk '{print $1}')"
elif command -v sha256sum >/dev/null 2>&1; then
  stage2_sha="$(sha256sum "$stage2" | awk '{print $1}')"
fi

if [ "$out" != "" ]; then
  out_dir="$(dirname "$out")"
  mkdir -p "$out_dir"
  cp "$stage2" "$out"
  if [ -f "$stage2.o" ]; then
    cp "$stage2.o" "$out.o"
  fi
fi

echo "backend_seed_pure ok: stage2=$stage2 sha256=$stage2_sha"

