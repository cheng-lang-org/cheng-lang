#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-${CHENG_V3_FRESH_NODE_COMPILER:-${CHENG_V3_SMOKE_COMPILER:-$default_compiler}}}"
label="${2:-${CHENG_V3_FRESH_NODE_LABEL:-host}}"

exec sh "$root/v3/tooling/cheng_v3.sh" run-fresh-node-selfhost-gate "--compiler:$compiler" "--label:$label"
