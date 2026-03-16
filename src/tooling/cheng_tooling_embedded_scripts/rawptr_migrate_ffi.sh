#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

impl="$root/src/tooling/rawptr_migrate_ffi.sh"
if [ ! -x "$impl" ]; then
  echo "[rawptr_migrate_ffi] missing implementation: $impl" 1>&2
  exit 1
fi

export TOOLING_EXEC_WRAPPER_CALLER="rawptr_migrate_ffi"
exec "$impl" "$@"
