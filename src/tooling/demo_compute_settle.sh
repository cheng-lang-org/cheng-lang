#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="build/cheng_demo_compute"
MODE="local"
EPOCH="1"
CLEAN="0"
RESET_LEDGER="0"
SETTLEMENT_BUDGET="2.0"
RWAD_CHAIN_ROOT="${RWAD_CHAIN_ROOT:-/Users/lbcheng/.cheng-packages/RWAD-blockchain}"
ALLOW_MISSING_RWAD="0"
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
    --clean)
      CLEAN="1"
      ;;
    --reset-ledger)
      RESET_LEDGER="1"
      ;;
    --points-budget:*)
      SETTLEMENT_BUDGET="${1#--points-budget:}"
      ;;
    --settlement-budget:*)
      SETTLEMENT_BUDGET="${1#--settlement-budget:}"
      ;;
    --allow-missing-rwad)
      ALLOW_MISSING_RWAD="1"
      ;;
    *)
      echo "unknown arg: $1" 1>&2
      exit 1
      ;;
  esac
  shift
done

if [ "$CLEAN" = "1" ]; then
  rm -rf "$ROOT"
fi
mkdir -p "$ROOT"

if [ ! -x "$storage_bin" ]; then
  src/tooling/chengc.sh src/tooling/cheng_storage.cheng --name:cheng_storage
fi

LEDGER="$ROOT/ledger.jsonl"
if [ "$RESET_LEDGER" = "1" ] && [ -f "$LEDGER" ]; then
  rm -f "$LEDGER"
fi

runtime_ok="1"
set +e
req_out="$("$storage_bin" exec --task:job-gpu-demo --package:pkg://demo/compute --author:node:alice --requester:node:app-1 \
  --gpu_ms:120000 --gpu_mem_bytes:8589934592 --gpu_count:1 --gpu_type:A10G --workload:infer \
  --price_gpu:0.00002 --price_gpu_mem:0.15 --epoch:"$EPOCH" --root:"$ROOT" --mode:"$MODE" 2>/dev/null)"
if [ $? -ne 0 ]; then
  runtime_ok="0"
fi
set -e

if [ "$runtime_ok" = "1" ]; then
  req_id="$(printf '%s' "$req_out" | sed -n 's/^exec ok: //p')"
  if [ -z "${req_id:-}" ]; then
    runtime_ok="0"
  fi
fi

if [ "$runtime_ok" = "1" ]; then
  set +e
  usage_out="$("$storage_bin" meter --task:job-gpu-demo --package:pkg://demo/compute --author:node:alice --executor:node:exec-1 \
    --gpu_ms:110000 --gpu_mem_bytes:7516192768 --gpu_count:1 --gpu_type:A10G --workload:infer \
    --price_gpu:0.00002 --price_gpu_mem:0.15 --royalty:0.12 --treasury:0.03 \
    --epoch:"$EPOCH" --root:"$ROOT" --mode:"$MODE" 2>/dev/null)"
  if [ $? -ne 0 ]; then
    runtime_ok="0"
  fi
  set -e
fi

if [ "$runtime_ok" = "1" ]; then
  usage_id="$(printf '%s' "$usage_out" | sed -n 's/^meter ok: //p')"
  if [ -z "${usage_id:-}" ]; then
    runtime_ok="0"
  fi
fi

if [ "$runtime_ok" = "1" ]; then
  set +e
  rec_out="$("$storage_bin" receipt --request:"$req_id" --task:job-gpu-demo --executor:node:exec-1 --status:ok \
    --usage:"$usage_id" --result:cid://result --epoch:"$EPOCH" --root:"$ROOT" --mode:"$MODE" 2>/dev/null)"
  if [ $? -ne 0 ]; then
    runtime_ok="0"
  fi
  set -e
fi

if [ "$runtime_ok" = "1" ]; then
  rec_id="$(printf '%s' "$rec_out" | sed -n 's/^receipt ok: //p')"
  if [ -z "${rec_id:-}" ]; then
    runtime_ok="0"
  fi
fi

if [ "$runtime_ok" = "1" ]; then
  set +e
  "$storage_bin" sample --root:"$ROOT" --epoch:"$EPOCH" --rate:1 --seed:demo \
    --auditor:node:auditor-1 --record --format:toml >/dev/null 2>/dev/null
  [ $? -ne 0 ] && runtime_ok="0"
  "$storage_bin" audit --task:job-gpu-demo --executor:node:exec-1 --auditor:node:auditor-1 --status:bad \
    --penalty:0.10 --epoch:"$EPOCH" --note:demo --root:"$ROOT" --mode:"$MODE" >/dev/null 2>/dev/null
  [ $? -ne 0 ] && runtime_ok="0"
  "$storage_bin" fraud --task:job-gpu-demo --executor:node:exec-1 --reporter:node:auditor-1 \
    --reason:demo --epoch:"$EPOCH" --root:"$ROOT" >/dev/null 2>/dev/null
  [ $? -ne 0 ] && runtime_ok="0"
  set -e
fi

if [ "$runtime_ok" != "1" ]; then
  echo "warn: runtime write path unavailable, fallback to empty-ledger bridge verification" 1>&2
  : > "$LEDGER"
fi

echo "settle:"
"$storage_bin" settle --root:"$ROOT" --format:toml --top:5 --reconcile-csv:"$ROOT/settle_reconcile.csv"

batch_id="cheng-epoch-$EPOCH"
sh src/tooling/cheng_rwad_bridge.sh export --root:"$ROOT" --epoch:"$EPOCH" --batch-id:"$batch_id" \
  --out:"$ROOT/rwad_batch.json" --top:0

rwad_tool="$RWAD_CHAIN_ROOT/tools/rwad_cheng_points_settle.sh"
if [ -x "$rwad_tool" ]; then
  "$rwad_tool" --batchFile:"$ROOT/rwad_batch.json" --budget:"$SETTLEMENT_BUDGET" \
    --stateIn:"$ROOT/rwad_points_state.json" --stateOut:"$ROOT/rwad_points_state.json" \
    --out:"$ROOT/rwad_result.json"
  sh src/tooling/cheng_rwad_bridge.sh apply --result:"$ROOT/rwad_result.json" \
    --batch:"$ROOT/rwad_batch.json" \
    --batch-id:"$batch_id" --require-status:finalized --out:"$ROOT/rwad_ack.json"
  echo "rwad-result:"
  cat "$ROOT/rwad_result.json"
else
  if [ "$ALLOW_MISSING_RWAD" = "1" ]; then
    echo "warn: missing RWAD tool: $rwad_tool" 1>&2
  else
    echo "error: missing RWAD tool: $rwad_tool" 1>&2
    echo "hint: pass --allow-missing-rwad for local-only demo mode" 1>&2
    exit 1
  fi
fi
