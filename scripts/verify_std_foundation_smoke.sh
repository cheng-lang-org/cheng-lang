#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [ -z "${CHENG_ROOT:-}" ]; then
  CHENG_ROOT="$ROOT"
fi

if [ -n "${CHENG_BACKEND_DRIVER:-}" ]; then
  BACKEND_DRIVER="$CHENG_BACKEND_DRIVER"
else
  for cand in \
    "$CHENG_ROOT/backend_mvp_driver" \
    "$CHENG_ROOT/backend_mvp_driver_libp2p" \
    "$CHENG_ROOT/artifacts/backend_selfhost/backend_mvp_driver.stage2" \
    "$CHENG_ROOT/artifacts/backend_selfhost/backend_mvp_driver_stage2" \
    "$CHENG_ROOT/artifacts/backend_seed/backend_mvp_driver.stage2"; do
    if [ -x "$cand" ]; then
      BACKEND_DRIVER="$cand"
      break
    fi
  done
fi

if [ -z "${BACKEND_DRIVER:-}" ] || [ ! -x "${BACKEND_DRIVER:-}" ]; then
  echo "[verify_std_foundation_smoke] missing backend driver under CHENG_ROOT=$CHENG_ROOT" 1>&2
  echo "[verify_std_foundation_smoke] hint: build backend_mvp_driver first" 1>&2
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
  cd "$CHENG_ROOT"
  CHENG_MM="${CHENG_MM:-off}" \
  CHENG_MM_ATOMIC="${CHENG_MM_ATOMIC:-0}" \
  CHENG_BACKEND_INPUT="$entry" \
  CHENG_BACKEND_OUTPUT="$out_path" \
  CHENG_BACKEND_EMIT="${CHENG_BACKEND_EMIT:-exe}" \
  CHENG_BACKEND_FRONTEND="${CHENG_BACKEND_FRONTEND:-stage1}" \
  CHENG_BACKEND_JOBS="${CHENG_BACKEND_JOBS:-1}" \
    exec "$BACKEND_DRIVER"
)

"$out_path"
echo "[verify_std_foundation_smoke] ok"

