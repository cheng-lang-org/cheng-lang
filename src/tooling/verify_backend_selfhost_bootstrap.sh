#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

# NOTE: asm backend removed. Keep this as a compatibility wrapper for older tooling
# that expects stage2 at `artifacts/backend_selfhost/backend_mvp_driver.stage2`.

set +e
sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
rc="$?"
set -e
if [ "$rc" -ne 0 ]; then
  exit "$rc"
fi

stage2_src="artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2"
stage2_dst_dir="artifacts/backend_selfhost"
stage2_dst="$stage2_dst_dir/backend_mvp_driver.stage2"

mkdir -p "$stage2_dst_dir"
cp "$stage2_src" "$stage2_dst"
if [ -s "$stage2_src.o" ]; then
  cp "$stage2_src.o" "$stage2_dst.o"
fi

echo "verify_backend_selfhost_bootstrap ok"
