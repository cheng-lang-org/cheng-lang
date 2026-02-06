#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [ ! -x ./stage1_runner ]; then
  echo "== Build stage1_runner =="
  sh src/tooling/bootstrap_pure.sh --skip-determinism
fi

tmp_ok="/tmp/diag_space_call_ok.c"
tmp_fail="/tmp/diag_space_call_fail.c"

if ! ./stage1_runner --mode:c --file:examples/diagnostics_space_call_ok.cheng --out:"$tmp_ok" >/dev/null 2>&1; then
  echo "[Error] expected space-call ok compile to succeed" 1>&2
  exit 1
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_space_call_fail.cheng --out:"$tmp_fail" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected space-call diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg "Space call does not allow parenthesized argument" >/dev/null
else
  echo "$output" | grep -F "Space call does not allow parenthesized argument" >/dev/null
fi

echo "space-call diagnostics ok"
