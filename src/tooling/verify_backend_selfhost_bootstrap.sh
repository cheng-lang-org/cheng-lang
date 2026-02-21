#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

# NOTE: asm backend removed. This script keeps a stable entrypoint name and
# delegates to the self-obj bootstrap verifier.

set +e
sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
rc="$?"
set -e
if [ "$rc" -ne 0 ]; then
  exit "$rc"
fi

stage2_src="artifacts/backend_selfhost_self_obj/cheng.stage2"
[ -x "$stage2_src" ]

echo "verify_backend_selfhost_bootstrap ok"
