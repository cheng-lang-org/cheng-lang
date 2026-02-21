#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

echo "[Error] legacy desktop C export pipeline has been removed." 1>&2
echo "  tip: use backend-only build via src/tooling/chengc.sh or src/tooling/chengb.sh" 1>&2
exit 2
