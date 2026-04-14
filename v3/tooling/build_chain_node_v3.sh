#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${CHENG_V3_CHAIN_NODE_COMPILER:-$root/artifacts/v3_backend_driver/cheng}"
src="$root/v3/src/project/chain_node_main.cheng"
target="${CHAIN_NODE_TARGET:-arm64-apple-darwin}"
default_out_dir="$root/artifacts/v3_chain_node"
if [ "$target" != "arm64-apple-darwin" ]; then
  default_out_dir="$default_out_dir/$target"
fi
out_dir="${CHAIN_NODE_OUT_DIR:-$default_out_dir}"
out_bin="${CHAIN_NODE_OUT_BIN:-$out_dir/chain_node}"
compile_log="$out_dir/chain_node.compile.log"
run_log="$out_dir/chain_node.self-test.log"

mkdir -p "$out_dir"

if [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 chain_node: missing backend driver: $compiler" >&2
  exit 1
fi

if [ ! -f "$src" ]; then
  echo "v3 chain_node: missing source: $src" >&2
  exit 1
fi

build_rc=0
set +e
"$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit exe \
  --target "$target" \
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

host_os="$(uname -s 2>/dev/null || true)"
host_arch="$(uname -m 2>/dev/null || true)"
host_target=""
case "$host_os/$host_arch" in
  Darwin/arm64)
    host_target="arm64-apple-darwin"
    ;;
  Linux/x86_64)
    host_target="x86_64-unknown-linux-gnu"
    ;;
  Linux/aarch64|Linux/arm64)
    host_target="aarch64-unknown-linux-gnu"
    ;;
esac

run_self_test="${CHAIN_NODE_RUN_SELF_TEST:-auto}"
if [ "$run_self_test" = "auto" ]; then
  if [ "$target" = "$host_target" ]; then
    run_self_test="1"
  else
    run_self_test="0"
  fi
fi

if [ "$run_self_test" != "0" ] && ! "$out_bin" self-test >"$run_log" 2>&1; then
  echo "v3 chain_node: self-test failed log=$run_log" >&2
  tail -n 80 "$run_log" >&2 || true
  exit 1
fi

if [ "$run_self_test" != "0" ]; then
  cat "$run_log"
else
  echo "v3 chain_node: build ok target=$target out=$out_bin"
fi
