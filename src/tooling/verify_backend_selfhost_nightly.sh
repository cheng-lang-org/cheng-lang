#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

env \
  CHENG_BACKEND_PROD_SELFHOST_MODE=fast \
  CHENG_BACKEND_RUN_SELFHOST_STRICT=1 \
  CHENG_BACKEND_PROD_SELFHOST_REUSE="${CHENG_BACKEND_PROD_SELFHOST_REUSE:-0}" \
  bash src/tooling/backend_prod_closure.sh --allow-skip --no-publish --fullchain "$@"
