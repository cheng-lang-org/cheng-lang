#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh"
exec sh "$tool" build-backend-driver "$@"
