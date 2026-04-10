#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
driver="$root/artifacts/v3_backend_driver/cheng"
src="$root/v3/src/project/chain_node_main.cheng"
out_dir="$root/artifacts/v3_chain_node"
out_bin="$out_dir/chain_node"
compile_log="$out_dir/chain_node.compile.log"
run_log="$out_dir/chain_node.self-test.log"

mkdir -p "$out_dir"

if [ ! -x "$driver" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$driver" ]; then
  echo "v3 chain_node: missing backend driver: $driver" >&2
  exit 1
fi

if [ ! -f "$src" ]; then
  echo "v3 chain_node: missing source: $src" >&2
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
  echo "v3 chain_node: backend driver failed rc=$build_rc log=$compile_log" >&2
  tail -n 80 "$compile_log" >&2 || true
  exit 1
fi

if [ ! -x "$out_bin" ]; then
  echo "v3 chain_node: backend driver returned success but no executable was produced: $out_bin" >&2
  exit 1
fi

if ! "$out_bin" >"$run_log" 2>&1; then
  echo "v3 chain_node: self-test failed log=$run_log" >&2
  tail -n 80 "$run_log" >&2 || true
  exit 1
fi

cat "$run_log"
