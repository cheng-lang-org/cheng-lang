#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_hostc="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-$default_hostc}"
label="${2:-host}"
src="$root/v3/src/tests/udp_importc_smoke.cheng"
out_dir="$root/artifacts/v3_udp_importc"
bin="$out_dir/udp_importc_smoke.$label"
compile_log="$out_dir/udp_importc_smoke.$label.compile.log"
run_log="$out_dir/udp_importc_smoke.$label.run.log"

mkdir -p "$out_dir"
cd "$root"

if [ "$compiler" = "$default_hostc" ] && [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 udp importc smoke: missing compiler: $compiler" >&2
  exit 1
fi

rm -f "$bin" "$bin".* "$compile_log" "$run_log"

echo "[v3 udp importc smoke] compile $label"
if ! DIAG_CONTEXT=1 "$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit exe \
  --target arm64-apple-darwin \
  --out "$bin" >"$compile_log" 2>&1; then
  echo "v3 udp importc smoke: compile failed: $label" >&2
  tail -n 80 "$compile_log" >&2 || true
  exit 1
fi

echo "[v3 udp importc smoke] run $label"
if ! "$bin" >"$run_log" 2>&1; then
  echo "v3 udp importc smoke: run failed: $label" >&2
  tail -n 80 "$run_log" >&2 || true
  exit 1
fi

cat "$run_log"
