#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
driver="$root/artifacts/v3_backend_driver/cheng"
src="$root/v3/src/tests/ordinary_call_chain_fixture.cheng"
out_dir="$root/artifacts/v3_call_chain"
out_bin="$out_dir/ordinary_call_chain_fixture"
compile_log="$out_dir/ordinary_call_chain_fixture.compile.log"
run_log="$out_dir/ordinary_call_chain_fixture.run.log"

mkdir -p "$out_dir"

if [ ! -x "$driver" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$driver" ]; then
  echo "v3 call-chain: missing backend driver: $driver" >&2
  exit 1
fi

if [ ! -f "$src" ]; then
  echo "v3 call-chain: missing source: $src" >&2
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
  echo "v3 call-chain: backend driver failed rc=$build_rc log=$compile_log" >&2
  tail -n 80 "$compile_log" >&2 || true
  exit 1
fi

if [ ! -x "$out_bin" ]; then
  echo "v3 call-chain: backend driver returned success but no executable was produced: $out_bin" >&2
  exit 1
fi

if ! "$out_bin" >"$run_log" 2>&1; then
  echo "v3 call-chain: built binary failed log=$run_log" >&2
  tail -n 80 "$run_log" >&2 || true
  exit 1
fi

cat "$run_log"
