#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [ ! -x ./stage1_runner ]; then
  echo "== Build stage1_runner =="
  sh src/tooling/bootstrap_pure.sh --skip-determinism
fi

tmp_ok="/tmp/diag_send_sync_ok.c"
tmp_fail="/tmp/diag_send_sync_fail.c"

if ! ./stage1_runner --mode:c --file:examples/diagnostics_send_sync_ok.cheng --out:"$tmp_ok" >/dev/null 2>&1; then
  echo "[Error] expected send/sync ok compile to succeed" 1>&2
  exit 1
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_send_sync_fail.cheng --out:"$tmp_fail" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected send/sync diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg "arcNew.*Send/Sync|Send/Sync.*arcNew" >/dev/null
  echo "$output" | rg "spawnJob.*Send/Sync|Send/Sync.*spawnJob" >/dev/null
else
  echo "$output" | grep -E "arcNew.*Send/Sync|Send/Sync.*arcNew" >/dev/null
  echo "$output" | grep -E "spawnJob.*Send/Sync|Send/Sync.*spawnJob" >/dev/null
fi

echo "send/sync diagnostics ok"
