#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")/../src/tooling" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ -z "${ROOT:-}" ]; then
  ROOT="$ROOT"
fi

if [ -n "${BACKEND_DRIVER:-}" ]; then
  BACKEND_DRIVER="$BACKEND_DRIVER"
else
  for cand in \
    "$ROOT/cheng" \
    "$ROOT/artifacts/backend_driver/cheng" \
    "$ROOT/dist/releases/current/cheng" \
    "$ROOT/artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "$ROOT/artifacts/backend_selfhost_self_obj/cheng.stage1" \
    "$ROOT/artifacts/backend_seed/cheng.stage2"; do
    if [ -x "$cand" ]; then
      BACKEND_DRIVER="$cand"
      break
    fi
  done
fi

if [ -z "${BACKEND_DRIVER:-}" ] || [ ! -x "${BACKEND_DRIVER:-}" ]; then
  echo "[verify_std_foundation_smoke] missing backend driver under ROOT=$ROOT" 1>&2
  echo "[verify_std_foundation_smoke] hint: build artifacts/backend_driver/cheng first" 1>&2
  exit 1
fi

entry="$ROOT/src/tests/std_foundation_smoke.cheng"
if [ ! -f "$entry" ]; then
  echo "[verify_std_foundation_smoke] missing test entry: $entry" 1>&2
  exit 1
fi

out_dir="$ROOT/artifacts/verify/std_foundation_smoke"
mkdir -p "$out_dir"
out_path="$out_dir/std_foundation_smoke"

(
  cd "$ROOT"
  MM="${MM:-off}" \
  MM_ATOMIC="${MM_ATOMIC:-0}" \
  BACKEND_INPUT="$entry" \
  BACKEND_OUTPUT="$out_path" \
  BACKEND_EMIT="${BACKEND_EMIT:-exe}" \
  BACKEND_FRONTEND="${BACKEND_FRONTEND:-stage1}" \
  BACKEND_JOBS="${BACKEND_JOBS:-1}" \
    exec "$BACKEND_DRIVER"
)

"$out_path"
echo "[verify_std_foundation_smoke] ok"
