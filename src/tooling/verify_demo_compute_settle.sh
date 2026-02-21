#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="build/cheng_demo_compute_verify"
MODE="local"
EPOCH="1"
repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

resolve_chengc_bin() {
  local name="$1"
  case "$name" in
    /*|*/*)
      printf '%s\n' "$name"
      return
      ;;
  esac
  printf '%s/artifacts/chengc/%s\n' "$repo_root" "$name"
}

storage_bin="$(resolve_chengc_bin cheng_storage)"

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

settle="$("$storage_bin" settle --root:"$ROOT" --format:toml --top:1)"
printf '%s' "$settle" | grep -Fq "compute_total" || { echo "verify: missing compute_total" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "audit_total" || { echo "verify: missing audit_total" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "exec_request" || { echo "verify: missing exec_request count" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "exec_receipt" || { echo "verify: missing exec_receipt count" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "audit_sample_count" || { echo "verify: missing audit_sample_count" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "fraud_report_count" || { echo "verify: missing fraud_report_count" 1>&2; exit 1; }

if [ ! -f "$ROOT/settle_reconcile.csv" ]; then
  echo "verify: missing reconcile csv" 1>&2
  exit 1
fi

if [ ! -f "$ROOT/rwad_batch.json" ]; then
  echo "verify: missing rwad_batch.json" 1>&2
  exit 1
fi

batch="$(cat "$ROOT/rwad_batch.json")"
printf '%s' "$batch" | grep -Fq "\"schema_version\"" || { echo "verify: missing schema_version" 1>&2; exit 1; }
printf '%s' "$batch" | grep -Fq "\"settlement\"" || { echo "verify: missing settlement" 1>&2; exit 1; }
printf '%s' "$batch" | grep -Fq "\"batch_id\"" || { echo "verify: missing batch_id" 1>&2; exit 1; }
printf '%s' "$batch" | grep -Fq "\"settlement_sha256\"" || { echo "verify: missing settlement_sha256" 1>&2; exit 1; }

if [ ! -f "$ROOT/rwad_result.json" ]; then
  echo "verify: missing rwad_result.json" 1>&2
  exit 1
fi
result="$(cat "$ROOT/rwad_result.json")"
printf '%s' "$result" | grep -Fq "\"schema_version\":\"rwad-cheng-settlement-result/v1\"" || { echo "verify: invalid rwad result schema" 1>&2; exit 1; }
printf '%s' "$result" | grep -Fq "\"request_schema\":\"cheng-rwad-settlement/v1\"" || { echo "verify: invalid rwad request schema" 1>&2; exit 1; }
printf '%s' "$result" | grep -Fq "\"status\":\"finalized\"" || { echo "verify: invalid rwad status" 1>&2; exit 1; }
printf '%s' "$result" | grep -Fq "\"totals\"" || { echo "verify: missing rwad totals" 1>&2; exit 1; }

if [ ! -f "$ROOT/rwad_ack.json" ]; then
  echo "verify: missing rwad_ack.json" 1>&2
  exit 1
fi
ack="$(cat "$ROOT/rwad_ack.json")"
printf '%s' "$ack" | grep -Fq "\"ok\"" || { echo "verify: missing rwad ack ok" 1>&2; exit 1; }
printf '%s' "$ack" | grep -Fq "\"request_schema\":\"cheng-rwad-settlement/v1\"" || { echo "verify: invalid rwad ack request schema" 1>&2; exit 1; }
printf '%s' "$ack" | grep -Fq "\"totals\"" || { echo "verify: missing rwad ack totals" 1>&2; exit 1; }

echo "verify ok"
