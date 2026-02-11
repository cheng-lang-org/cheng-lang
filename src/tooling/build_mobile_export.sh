#!/usr/bin/env sh
set -eu

echo "[Error] legacy mobile C export pipeline has been removed." 1>&2
echo "  tip: use backend-only build via src/tooling/chengb.sh --target:<triple>" 1>&2
exit 2
