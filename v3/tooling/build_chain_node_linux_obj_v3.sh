#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${CHENG_V3_CHAIN_NODE_COMPILER:-$root/artifacts/v3_backend_driver/cheng}"
src="$root/v3/src/project/chain_node_main.cheng"
target="${CHAIN_NODE_OBJ_TARGET:-aarch64-unknown-linux-gnu}"
default_out_dir="$root/artifacts/v3_chain_node_obj/$target"
out_dir="${CHAIN_NODE_OBJ_OUT_DIR:-$default_out_dir}"
out_obj="${CHAIN_NODE_OBJ_OUT:-$out_dir/chain_node.o}"
compile_log="$out_dir/chain_node.compile.log"

case "$target" in
  aarch64-unknown-linux-gnu|arm64-unknown-linux-gnu)
    target="aarch64-unknown-linux-gnu"
    ;;
  *)
    echo "v3 chain_node linux obj: unsupported target=$target" >&2
    echo "v3 chain_node linux obj: current v3 seed linux object path is only verified on generic aarch64" >&2
    echo "v3 chain_node linux obj: x86_64 still only supports simple object smoke, not full ordinary chain_node" >&2
    exit 1
    ;;
esac

mkdir -p "$out_dir"

if [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 chain_node linux obj: missing backend driver: $compiler" >&2
  exit 1
fi

if [ ! -f "$src" ]; then
  echo "v3 chain_node linux obj: missing source: $src" >&2
  exit 1
fi

build_rc=0
set +e
"$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$src" \
  --emit obj \
  --target "$target" \
  --out "$out_obj" >"$compile_log" 2>&1
build_rc="$?"
set -e

if [ "$build_rc" -ne 0 ]; then
  echo "v3 chain_node linux obj: backend driver failed rc=$build_rc log=$compile_log" >&2
  tail -n 80 "$compile_log" >&2 || true
  exit 1
fi

if [ ! -f "$out_obj" ]; then
  echo "v3 chain_node linux obj: backend driver returned success but no object was produced: $out_obj" >&2
  exit 1
fi

echo "v3 chain_node linux obj: build ok target=$target out=$out_obj"
