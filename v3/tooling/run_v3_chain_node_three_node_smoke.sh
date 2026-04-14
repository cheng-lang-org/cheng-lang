#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
driver="${CHENG_V3_GATE_DRIVER:-$root/artifacts/v3_backend_driver/cheng}"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-${CHENG_V3_CHAIN_NODE_COMPILER:-${CHENG_V3_SMOKE_COMPILER:-$default_compiler}}}"
label="${2:-${CHENG_V3_CHAIN_NODE_LABEL:-host}}"

if [ ! -x "$driver" ]; then
  echo "v3 chain_node three-node smoke: missing backend driver: $driver" >&2
  exit 1
fi

exec "$driver" run-chain-node-three-node-smoke "--compiler:$compiler" "--label:$label"
