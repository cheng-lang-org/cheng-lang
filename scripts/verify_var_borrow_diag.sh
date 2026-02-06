#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

if [ ! -x ./stage1_runner ]; then
  echo "== Build stage1_runner =="
  sh src/tooling/bootstrap_pure.sh --skip-determinism
fi

tmp_out="/tmp/diag_var_borrow.c"
tmp_ok="/tmp/diag_var_borrow_ok.c"
tmp_origin_ok="/tmp/diag_borrow_origin_ok.c"
tmp_origin_use="/tmp/diag_borrow_origin_use.c"
tmp_origin_write="/tmp/diag_borrow_origin_write.c"
tmp_escape_return="/tmp/diag_borrow_escape_return.c"
tmp_escape_share="/tmp/diag_borrow_escape_share.c"
tmp_escape_call="/tmp/diag_borrow_escape_call.c"
tmp_escape_global="/tmp/diag_borrow_escape_global_assign.c"
tmp_escape_tableput="/tmp/diag_borrow_escape_tableput.c"

set +e
first_out="$(./stage1_runner --mode:c --file:examples/diagnostics_var_borrow_ok.cheng --out:"$tmp_ok" 2>&1)"
first_status=$?
set -e

if [ "$first_status" -ne 0 ]; then
  echo "[Error] expected var borrow ok compile to succeed" 1>&2
  exit 1
fi

if ! ./stage1_runner --mode:c --file:examples/diagnostics_borrow_origin_ok.cheng --out:"$tmp_origin_ok" >/dev/null 2>&1; then
  echo "[Error] expected borrow origin ok compile to succeed" 1>&2
  exit 1
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_var_borrow.cheng --out:"$tmp_out" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected var borrow diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "lvalue" >/dev/null
  echo "$output" | rg -F "inc" >/dev/null
else
  echo "$output" | grep -F "lvalue" >/dev/null
  echo "$output" | grep -F "inc" >/dev/null
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_borrow_origin_use.cheng --out:"$tmp_origin_use" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected borrow origin use diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "Cannot use while borrowed" >/dev/null
  echo "$output" | rg -F "x" >/dev/null
else
  echo "$output" | grep -F "Cannot use while borrowed" >/dev/null
  echo "$output" | grep -F "x" >/dev/null
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_borrow_origin_write.cheng --out:"$tmp_origin_write" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected borrow origin write diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "Cannot write while borrowed" >/dev/null
  echo "$output" | rg -F "x" >/dev/null
else
  echo "$output" | grep -F "Cannot write while borrowed" >/dev/null
  echo "$output" | grep -F "x" >/dev/null
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_borrow_escape_return.cheng --out:"$tmp_escape_return" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected borrow escape (return) diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "Borrowed value cannot be returned" >/dev/null
else
  echo "$output" | grep -F "Borrowed value cannot be returned" >/dev/null
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_borrow_escape_share.cheng --out:"$tmp_escape_share" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected borrow escape (share) diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "Borrowed value cannot be shared" >/dev/null
else
  echo "$output" | grep -F "Borrowed value cannot be shared" >/dev/null
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_borrow_escape_call.cheng --out:"$tmp_escape_call" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected borrow escape (call) diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "Borrowed value cannot be passed to non-var parameter" >/dev/null
  echo "$output" | rg -F "take" >/dev/null
else
  echo "$output" | grep -F "Borrowed value cannot be passed to non-var parameter" >/dev/null
  echo "$output" | grep -F "take" >/dev/null
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_borrow_escape_global_assign.cheng --out:"$tmp_escape_global" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected borrow escape (global assign) diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "Borrowed value cannot be written to global" >/dev/null
  echo "$output" | rg -F "g" >/dev/null
else
  echo "$output" | grep -F "Borrowed value cannot be written to global" >/dev/null
  echo "$output" | grep -F "g" >/dev/null
fi

set +e
output="$(./stage1_runner --mode:c --file:examples/diagnostics_borrow_escape_tableput.cheng --out:"$tmp_escape_tableput" 2>&1)"
status=$?
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected borrow escape (TablePut) diagnostic but compile succeeded" 1>&2
  exit 1
fi

if command -v rg >/dev/null 2>&1; then
  echo "$output" | rg -F "Borrowed value cannot be passed to non-var parameter" >/dev/null
  echo "$output" | rg -F "TablePut" >/dev/null
else
  echo "$output" | grep -F "Borrowed value cannot be passed to non-var parameter" >/dev/null
  echo "$output" | grep -F "TablePut" >/dev/null
fi

echo "var borrow diagnostics ok"
