#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail
tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
exec "$tool" verify_backend_targets "$@"
