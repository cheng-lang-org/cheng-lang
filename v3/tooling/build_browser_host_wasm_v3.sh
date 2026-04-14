#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${CHENG_V3_WASM_COMPILER:-${CHENG_V3_SMOKE_COMPILER:-$default_compiler}}"
src="$root/v3/src/libp2p/browser/browser_host_wasm_probe.cheng"
out="${1:-$root/artifacts/v3_browser_host_wasm/cheng_browser_host_abi.wasm}"

if [ "$compiler" = "$default_compiler" ] && [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

mkdir -p "$(dirname "$out")"

exec "$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit exe \
  --target wasm32-unknown-unknown \
  --out "$out"
