#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

env \
  BACKEND_PROD_SELFHOST_MODE=fast \
  BACKEND_RUN_SELFHOST_STRICT=1 \
  BACKEND_PROD_SELFHOST_REUSE="${BACKEND_PROD_SELFHOST_REUSE:-0}" \
  bash src/tooling/backend_prod_closure.sh --allow-skip --no-publish --fullchain "$@"
