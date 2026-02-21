#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="build/cheng_rwad_contract_verify"
BATCH_ID="cheng-epoch-9"
EPOCH="9"
RWAD_CHAIN_ROOT="${RWAD_CHAIN_ROOT:-/Users/lbcheng/.cheng-packages/RWAD-blockchain}"

while [ $# -gt 0 ]; do
  case "$1" in
    --root:*)
      ROOT="${1#--root:}"
      ;;
    --batch-id:*)
      BATCH_ID="${1#--batch-id:}"
      ;;
    --epoch:*)
      EPOCH="${1#--epoch:}"
      ;;
    --rwad-root:*)
      RWAD_CHAIN_ROOT="${1#--rwad-root:}"
      ;;
    *)
      echo "unknown arg: $1" 1>&2
      exit 1
      ;;
  esac
  shift
done

rwad_tool="$RWAD_CHAIN_ROOT/tools/rwad_cheng_points_settle.sh"
if [ ! -x "$rwad_tool" ]; then
  echo "verify: missing RWAD tool: $rwad_tool" 1>&2
  exit 1
fi

rm -rf "$ROOT"
mkdir -p "$ROOT"

cat > "$ROOT/rwad_batch.json" <<EOF
{"schema_version":"cheng-rwad-settlement/v1","source":"cheng-lang/verify_rwad_interface_contract.sh","batch_id":"$BATCH_ID","epoch":$EPOCH,"ledger_path":"$ROOT/ledger.jsonl","settlement_sha256":"fixture-sha256","settlement":{"epoch":$EPOCH,"storage_total":1.2,"compute_total":4.5,"treasury_total":0.2,"audit_total":0.1,"counts":{"exec_request":3,"exec_receipt":3},"payouts":{"authors":{"node:alice":2.1,"node:bob":0.6},"providers":{"node:storage-1":1.4},"executors":{"node:exec-1":1.9},"penalties":{"node:exec-1":0.3}},"trust":{"fraud_by_executor":{"node:exec-1":0.0},"storage_ok_by_provider":{"node:storage-1":1.0},"storage_missing_by_provider":{"node:storage-2":0.0}}}}
EOF

"$rwad_tool" \
  --batchFile:"$ROOT/rwad_batch.json" \
  --budget:3.0 \
  --stateOut:"$ROOT/rwad_points_state.json" \
  --out:"$ROOT/rwad_result.json"

sh src/tooling/cheng_rwad_bridge.sh apply \
  --result:"$ROOT/rwad_result.json" \
  --batch:"$ROOT/rwad_batch.json" \
  --batch-id:"$BATCH_ID" \
  --require-status:finalized \
  --out:"$ROOT/rwad_ack.json"

result="$(cat "$ROOT/rwad_result.json")"
printf '%s' "$result" | grep -Fq "\"schema_version\":\"rwad-cheng-settlement-result/v1\"" || { echo "verify: invalid result schema" 1>&2; exit 1; }
printf '%s' "$result" | grep -Fq "\"request_schema\":\"cheng-rwad-settlement/v1\"" || { echo "verify: invalid request schema" 1>&2; exit 1; }
printf '%s' "$result" | grep -Fq "\"settlement_sha256\"" || { echo "verify: missing settlement_sha256 in result" 1>&2; exit 1; }
printf '%s' "$result" | grep -Fq "\"batch_id\":\"$BATCH_ID\"" || { echo "verify: batch id mismatch in result" 1>&2; exit 1; }

ack="$(cat "$ROOT/rwad_ack.json")"
printf '%s' "$ack" | grep -Fq "\"schema_version\":\"cheng-rwad-ack/v1\"" || { echo "verify: invalid ack schema" 1>&2; exit 1; }
printf '%s' "$ack" | grep -Fq "\"request_schema\":\"cheng-rwad-settlement/v1\"" || { echo "verify: missing request_schema in ack" 1>&2; exit 1; }
printf '%s' "$ack" | grep -Fq "\"totals\"" || { echo "verify: missing totals in ack" 1>&2; exit 1; }
printf '%s' "$ack" | grep -Fq "\"batch_id\":\"$BATCH_ID\"" || { echo "verify: batch id mismatch in ack" 1>&2; exit 1; }

echo "verify contract ok"
