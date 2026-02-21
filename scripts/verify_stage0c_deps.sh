#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")/../src/tooling" && pwd)/env_prefix_bridge.sh"
set -eu

echo "[Error] this legacy verification script has been removed." 1>&2
echo "  tip: run backend checks under src/tooling/verify_backend_*.sh" 1>&2
exit 2
