#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_backend_exe_determinism_strict
