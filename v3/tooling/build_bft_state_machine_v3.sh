#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${CHENG_V3_BFT_COMPILER:-$root/artifacts/v3_backend_driver/cheng}"
src="$root/v3/src/project/bft_state_machine_main.cheng"
out_dir="$root/artifacts/v3_bft_state_machine"
out_bin="$out_dir/bft_state_machine"
compile_log="$out_dir/bft_state_machine.compile.log"
run_log="$out_dir/bft_state_machine.self-test.log"

mkdir -p "$out_dir"

if [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

"$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit exe \
  --target arm64-apple-darwin \
  --out "$out_bin" >"$compile_log" 2>&1

"$out_bin" >"$run_log" 2>&1
cat "$run_log"
