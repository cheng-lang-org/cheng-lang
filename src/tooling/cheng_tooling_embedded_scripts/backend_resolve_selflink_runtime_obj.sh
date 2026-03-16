#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

export TOOLING_EXEC_WRAPPER_CALLER="backend_resolve_selflink_runtime_obj"
exec ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_resolve_selflink_runtime_obj "$@"
