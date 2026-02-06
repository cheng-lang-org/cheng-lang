#!/usr/bin/env sh
set -eu

echo "[Error] stage0c has been removed from the production toolchain." 1>&2
echo "  tip: use src/tooling/chengc.sh (default --backend:obj) or src/tooling/chengb.sh" 1>&2
exit 2
