#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
export CHAIN_NODE_LINUX_ARTIFACT="${CHAIN_NODE_LINUX_ARTIFACT:-obj}"
exec sh "$root/v3/tooling/cheng_v3.sh" build-chain-node-linux "$@"
