#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="build/cheng_demo_verify"
MODE="local"
LISTEN=""
PEER=""
REQUIRE_LEASE="0"
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
    --listen:*)
      LISTEN="${1#--listen:}"
      ;;
    --peer:*)
      PEER="${1#--peer:}"
      ;;
    --require-lease)
      REQUIRE_LEASE="1"
      ;;
    *)
      echo "unknown arg: $1" 1>&2
      exit 1
      ;;
  esac
  shift
done

DEMO_ARGS=(--root:"$ROOT" --mode:"$MODE" --clean --reset-ledger --regen-lease)
if [ "$REQUIRE_LEASE" = "1" ]; then
  DEMO_ARGS+=(--require-lease)
fi
if [ -n "$LISTEN" ]; then
  DEMO_ARGS+=(--listen:"$LISTEN")
fi
if [ -n "$PEER" ]; then
  DEMO_ARGS+=(--peer:"$PEER")
fi

sh src/tooling/demo_io_lease.sh "${DEMO_ARGS[@]}" >/dev/null

settle="$("$storage_bin" settle --root:"$ROOT" --format:toml --top:1)"
printf '%s' "$settle" | grep -Fq "[payouts.authors]" || { echo "verify: missing payouts.authors" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "[payouts.providers]" || { echo "verify: missing payouts.providers" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "storage_total" || { echo "verify: missing storage_total" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "\"node:alice\"" || { echo "verify: missing author payout" 1>&2; exit 1; }
printf '%s' "$settle" | grep -Fq "\"node:store-1\"" || { echo "verify: missing provider payout" 1>&2; exit 1; }

echo "verify ok"
