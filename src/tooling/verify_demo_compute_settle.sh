#!/usr/bin/env bash
set -euo pipefail

ROOT="build/cheng_demo_compute_verify"
MODE="local"
EPOCH="1"

while [ $# -gt 0 ]; do
  case "$1" in
    --root:*)
      ROOT="${1#--root:}"
      ;;
    --mode:*)
      MODE="${1#--mode:}"
      ;;
    --epoch:*)
      EPOCH="${1#--epoch:}"
      ;;
    *)
      echo "unknown arg: $1" 1>&2
      exit 1
      ;;
  esac
  shift
done

DEMO_ARGS=(--root:"$ROOT" --mode:"$MODE" --epoch:"$EPOCH" --clean --reset-ledger)
sh src/tooling/demo_compute_settle.sh "${DEMO_ARGS[@]}" >/dev/null

settle="$(./cheng_storage settle --root:"$ROOT" --format:toml --top:1)"
printf '%s' "$settle" | grep -Fq "compute_total" || { echo "verify: missing compute_total" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "audit_total" || { echo "verify: missing audit_total" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "exec_request" || { echo "verify: missing exec_request count" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "exec_receipt" || { echo "verify: missing exec_receipt count" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "audit_sample_count" || { echo "verify: missing audit_sample_count" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "fraud_report_count" || { echo "verify: missing fraud_report_count" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "\"node:exec-1\"" || { echo "verify: missing executor payout" 1>&2; exit 1; }

if [ ! -f "$ROOT/settle_reconcile.csv" ]; then
  echo "verify: missing reconcile csv" 1>&2
  exit 1
fi

echo "verify ok"
