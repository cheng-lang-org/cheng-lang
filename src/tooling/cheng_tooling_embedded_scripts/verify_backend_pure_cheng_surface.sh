#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh"
bin="${TOOLING_BIN:-$root/artifacts/tooling_cmd/cheng_tooling.pure_cheng_surface_exec.bin}"

exec env \
  TOOLING_BIN="$bin" \
  TOOLING_FORCE_BUILD="${TOOLING_FORCE_BUILD:-1}" \
  TOOLING_COMPILE_TIMEOUT="${TOOLING_COMPILE_TIMEOUT:-60}" \
  TOOLING_EMIT_SELFHOST_LAUNCHER=0 \
  sh "$tool" verify_backend_pure_cheng_surface "$@"
