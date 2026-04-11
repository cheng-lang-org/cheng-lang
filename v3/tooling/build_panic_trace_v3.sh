#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
driver="$root/artifacts/v3_backend_driver/cheng"
src="$root/v3/src/tests/ordinary_panic_fixture.cheng"
out_dir="$root/artifacts/v3_panic_trace"
out_bin="$out_dir/ordinary_panic_fixture"
line_map="$out_bin.v3.map"
compile_log="$out_dir/ordinary_panic_fixture.compile.log"
run_log="$out_dir/ordinary_panic_fixture.run.log"

mkdir -p "$out_dir"

if [ ! -x "$driver" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$driver" ]; then
  echo "v3 panic-trace: missing backend driver: $driver" >&2
  exit 1
fi

if [ ! -f "$src" ]; then
  echo "v3 panic-trace: missing source: $src" >&2
  exit 1
fi

build_rc=0
set +e
"$driver" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit exe \
  --target arm64-apple-darwin \
  --out "$out_bin" >"$compile_log" 2>&1
build_rc="$?"
set -e

if [ "$build_rc" -ne 0 ]; then
  echo "v3 panic-trace: backend driver failed rc=$build_rc log=$compile_log" >&2
  tail -n 80 "$compile_log" >&2 || true
  exit 1
fi

if [ ! -x "$out_bin" ]; then
  echo "v3 panic-trace: backend driver returned success but no executable was produced: $out_bin" >&2
  exit 1
fi

if [ ! -f "$line_map" ]; then
  echo "v3 panic-trace: missing line-map sidecar: $line_map" >&2
  exit 1
fi

if ! rg -q '^v3_line_map_v1$' "$line_map"; then
  echo "v3 panic-trace: invalid line-map header: $line_map" >&2
  exit 1
fi

if ! rg -q 'ordinary_panic_fixture\.cheng' "$line_map"; then
  echo "v3 panic-trace: line-map missing fixture source path: $line_map" >&2
  exit 1
fi

run_rc=0
set +e
"$out_bin" >"$run_log" 2>&1
run_rc="$?"
set -e

if [ "$run_rc" -eq 0 ]; then
  echo "v3 panic-trace: expected non-zero exit from panic fixture" >&2
  exit 1
fi

if ! rg -q '^v3 panic fixture$' "$run_log"; then
  echo "v3 panic-trace: panic message missing: $run_log" >&2
  cat "$run_log" >&2 || true
  exit 1
fi

if ! rg -q '^\[cheng-v3\] machine-trace reason=panic mode=backtrace$' "$run_log"; then
  echo "v3 panic-trace: machine trace header missing: $run_log" >&2
  cat "$run_log" >&2 || true
  exit 1
fi

if ! rg -q '^\[cheng-v3\] m#[0-9]+ .*ordinary_panic_fixture\.cheng:[0-9]+' "$run_log"; then
  echo "v3 panic-trace: machine trace missing fixture line info: $run_log" >&2
  cat "$run_log" >&2 || true
  exit 1
fi

if ! rg -q 'ordinary_panic_fixture\.cheng:[0-9]+' "$run_log"; then
  echo "v3 panic-trace: source trace missing fixture line info: $run_log" >&2
  cat "$run_log" >&2 || true
  exit 1
fi

cat "$run_log"
