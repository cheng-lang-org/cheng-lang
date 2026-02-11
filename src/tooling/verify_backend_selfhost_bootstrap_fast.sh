#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

exec env CHENG_SELF_OBJ_BOOTSTRAP_MODE=fast sh src/tooling/verify_backend_selfhost_bootstrap.sh "$@"
