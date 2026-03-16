#!/usr/bin/env sh
:
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

echo "[Warn] src/tooling/bootstrap.sh has been migrated to pure obj/exe bootstrap." 1>&2
echo "       forwarding to src/tooling/bootstrap_pure.sh" 1>&2

exec ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} bootstrap_pure "$@"
