#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${CHENG_V3_LINUX_OBJ_COMPILER:-$root/artifacts/v3_backend_driver/cheng}"
out_dir="${CHENG_V3_LINUX_OBJ_SMOKE_OUT_DIR:-$root/artifacts/v3_linux_object_smokes}"
a64_chain_dir="$out_dir/chain_node_aarch64"
a64_rwad_dir="$out_dir/rwad_bft_aarch64"

find_llvm_objdump() {
  if command -v xcrun >/dev/null 2>&1; then
    p="$(xcrun --find llvm-objdump 2>/dev/null || true)"
    if [ "$p" != "" ]; then
      printf "%s\n" "$p"
      return 0
    fi
  fi
  for n in llvm-objdump llvm-objdump-19 llvm-objdump-18 llvm-objdump-17 llvm-objdump-16 llvm-objdump-15 llvm-objdump-14; do
    if command -v "$n" >/dev/null 2>&1; then
      command -v "$n"
      return 0
    fi
  done
  return 1
}

mkdir -p "$out_dir"

if [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 linux object smokes: missing backend driver: $compiler" >&2
  exit 1
fi

objdump_tool="$(find_llvm_objdump || true)"
if [ "$objdump_tool" = "" ]; then
  echo "v3 linux object smokes: missing llvm-objdump" >&2
  exit 1
fi

CHAIN_NODE_OBJ_TARGET=aarch64-unknown-linux-gnu \
CHAIN_NODE_OBJ_OUT_DIR="$a64_chain_dir" \
CHENG_V3_CHAIN_NODE_COMPILER="$compiler" \
sh "$root/v3/tooling/build_chain_node_linux_obj_v3.sh"

RWAD_BFT_OBJ_TARGET=aarch64-unknown-linux-gnu \
RWAD_BFT_OBJ_OUT_DIR="$a64_rwad_dir" \
CHENG_V3_RWAD_BFT_COMPILER="$compiler" \
sh "$root/v3/tooling/build_rwad_bft_state_machine_linux_obj_v3.sh"

file "$a64_chain_dir/chain_node.o" | grep -F "ELF 64-bit LSB relocatable, ARM aarch64" >/dev/null
file "$a64_rwad_dir/rwad_bft_state_machine.o" | grep -F "ELF 64-bit LSB relocatable, ARM aarch64" >/dev/null
"$objdump_tool" -f "$a64_chain_dir/chain_node.o" | grep -F "architecture: aarch64" >/dev/null
"$objdump_tool" -f "$a64_rwad_dir/rwad_bft_state_machine.o" | grep -F "architecture: aarch64" >/dev/null

echo "v3 linux object smokes: ok"
