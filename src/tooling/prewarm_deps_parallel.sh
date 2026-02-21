#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

echo "[Error] legacy deps prewarm pipeline has been removed." 1>&2
echo "  tip: backend-only pipeline no longer requires this step" 1>&2
exit 2
