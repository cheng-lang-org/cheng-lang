#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")/../../tooling" && pwd)/env_prefix_bridge.sh"
set -eu

echo "[Error] legacy native C server builder has been removed." 1>&2
echo "  tip: build backend executables via src/tooling/chengb.sh" 1>&2
exit 2
