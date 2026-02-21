#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

exec env SELF_OBJ_BOOTSTRAP_MODE=fast sh src/tooling/verify_backend_selfhost_bootstrap.sh "$@"
