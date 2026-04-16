#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="${TOOLING_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)}"
cd "$root"
exec sh "$root/v3/tooling/build_backend_driver_v3.sh" "$@"
