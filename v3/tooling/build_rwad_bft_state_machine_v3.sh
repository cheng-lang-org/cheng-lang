#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${CHENG_V3_RWAD_COMPILER:-$root/artifacts/v3_backend_driver/cheng}"
src="$root/v3/src/project/rwad_bft_state_machine_main.cheng"
target="${RWAD_BFT_TARGET:-arm64-apple-darwin}"
default_out_dir="$root/artifacts/v3_rwad_bft_state_machine"
if [ "$target" != "arm64-apple-darwin" ]; then
  default_out_dir="$default_out_dir/$target"
fi
out_dir="${RWAD_BFT_OUT_DIR:-$default_out_dir}"
out_bin="${RWAD_BFT_OUT_BIN:-$out_dir/rwad_bft_state_machine}"
compile_log="$out_dir/rwad_bft_state_machine.compile.log"
run_log="$out_dir/rwad_bft_state_machine.self-test.log"

mkdir -p "$out_dir"

if [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

"$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit exe \
  --target "$target" \
  --out "$out_bin" >"$compile_log" 2>&1

run_self_test="${RWAD_BFT_RUN_SELF_TEST:-auto}"
if [ "$run_self_test" = "auto" ]; then
  if [ "$target" = "arm64-apple-darwin" ]; then
    run_self_test="1"
  else
    run_self_test="0"
  fi
fi

if [ "$run_self_test" != "0" ]; then
  "$out_bin" self-test >"$run_log" 2>&1
  cat "$run_log"
else
  echo "v3 rwad_bft_state_machine: build ok target=$target out=$out_bin"
fi
