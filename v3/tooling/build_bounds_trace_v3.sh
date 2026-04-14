#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
exec sh "$root/v3/tooling/cheng_v3.sh" build-bounds-trace "$@"
