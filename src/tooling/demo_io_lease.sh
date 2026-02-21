#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="build/cheng_demo"
MODE="local"
LISTEN=""
PEER=""
REQUIRE_LEASE="0"
CLEAN="0"
RESET_LEDGER="0"
FAIL_WITHOUT_LEASE="0"
REGEN_LEASE="0"
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
registry_bin="$(resolve_chengc_bin cheng_registry)"

while [ $# -gt 0 ]; do
  case "$1" in
    --root:*)
      ROOT="${1#--root:}"
      ;;
    --mode:*)
      MODE="${1#--mode:}"
      ;;
    --listen:*)
      LISTEN="${1#--listen:}"
      ;;
    --peer:*)
      PEER="${1#--peer:}"
      ;;
    --require-lease)
      REQUIRE_LEASE="1"
      ;;
    --clean)
      CLEAN="1"
      ;;
    --reset-ledger)
      RESET_LEDGER="1"
      ;;
    --fail-without-lease)
      FAIL_WITHOUT_LEASE="1"
      ;;
    --regen-lease)
      REGEN_LEASE="1"
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

if [ ! -x "$registry_bin" ]; then
  src/tooling/chengc.sh src/tooling/cheng_registry.cheng --name:cheng_registry
fi

KEY_JSON="$ROOT/keypair.json"
if [ ! -f "$KEY_JSON" ]; then
  "$registry_bin" keygen --out:"$KEY_JSON"
fi

LEDGER="$ROOT/ledger.jsonl"
if [ "$RESET_LEDGER" = "1" ] && [ -f "$LEDGER" ]; then
  rm -f "$LEDGER"
fi
priv="$(sed -n 's/.*"priv_key"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p' "$KEY_JSON")"
if [ -z "${priv:-}" ]; then
  echo "demo: missing priv_key" 1>&2
  exit 1
fi

LEASE_TOKEN="$ROOT/lease-token.json"
if [ "$REGEN_LEASE" = "1" ] || [ ! -f "$LEASE_TOKEN" ]; then
  "$storage_bin" leasegen --package:pkg://demo/text --author:node:alice --provider:node:store-1 \
    --bytes:1048576 --days:30 --replicas:1 --price:0.25 --royalty:0.12 --treasury:0.03 \
    --priv:"$priv" --out:"$LEASE_TOKEN"
fi

if [ "$REQUIRE_LEASE" = "1" ]; then
  export IO_REQUIRE_LEASE=1
fi
if [ "$FAIL_WITHOUT_LEASE" = "1" ] && [ "$REQUIRE_LEASE" != "1" ]; then
  REQUIRE_LEASE="1"
  export IO_REQUIRE_LEASE=1
fi

if [ "$MODE" = "p2p" ]; then
  if [ -z "$LISTEN" ] && [ -z "$PEER" ]; then
    echo "demo: p2p requires --listen or --peer" 1>&2
    exit 1
  fi
fi

PUT_ARGS=(--root:"$ROOT" --mode:"$MODE")
if [ -n "$LISTEN" ]; then
  PUT_ARGS+=(--listen:"$LISTEN")
fi
if [ -n "$PEER" ]; then
  PUT_ARGS+=(--peer:"$PEER")
fi

if [ "$FAIL_WITHOUT_LEASE" = "1" ]; then
  echo "expect failure without lease:"
  set +e
  "$storage_bin" put-text --text:"no lease" "${PUT_ARGS[@]}"
  rc=$?
  set -e
  if [ "$rc" -eq 0 ]; then
    echo "demo: expected failure but succeeded" 1>&2
    exit 1
  fi
fi

cid="$("$storage_bin" put-text --text:"hello lease" --lease:"$LEASE_TOKEN" "${PUT_ARGS[@]}")"
echo "cid: $cid"

echo "cat:"
"$storage_bin" cat --cid:"$cid" "${PUT_ARGS[@]}"

echo "cat raw:"
"$storage_bin" cat --cid:"$cid" --raw "${PUT_ARGS[@]}"

echo "settle:"
echo "note: payouts are under payouts.authors/providers"
"$storage_bin" settle --root:"$ROOT" --format:toml --top:5
echo "settle (yaml):"
"$storage_bin" settle --root:"$ROOT" --format:yaml --top:5
