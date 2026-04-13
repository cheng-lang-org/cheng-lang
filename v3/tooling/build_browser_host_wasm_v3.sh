#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${CHENG_V3_WASM_COMPILER:-$default_compiler}"
src="$root/v3/src/libp2p/browser/browser_host_wasm_probe.cheng"
out="${1:-$root/artifacts/v3_browser_host_wasm/cheng_browser_host_abi.wasm}"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/cheng-v3-browser-wasm.XXXXXX")"
tmp_out="$tmp_dir/cheng_browser_host_abi.wasm"

cleanup() {
  rm -rf "$tmp_dir"
}

trap cleanup EXIT INT TERM

if [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 browser host wasm: missing compiler: $compiler" >&2
  exit 1
fi

mkdir -p "$(dirname "$out")"

"$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit exe \
  --target wasm32-unknown-unknown \
  --out "$tmp_out"

cp "$tmp_out" "$out"
file "$out"
