#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
export RWAD_BFT_LINUX_ARTIFACT="${RWAD_BFT_LINUX_ARTIFACT:-obj}"
exec sh "$root/v3/tooling/cheng_v3.sh" build-rwad-bft-linux "$@"
