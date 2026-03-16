#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

env \
  BACKEND_PROD_SELFHOST_MODE=fast \
  BACKEND_RUN_SELFHOST_STRICT=1 \
  BACKEND_PROD_SELFHOST_REUSE="${BACKEND_PROD_SELFHOST_REUSE:-0}" \
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_prod_closure --allow-skip --no-publish --fullchain "$@"
