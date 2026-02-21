#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

mm="${MM:-orc}"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

# Prefer a ready stage2 driver to avoid slow local rebuilds in CI/gates.
if [ "${BACKEND_DRIVER:-}" = "" ]; then
  if [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
    export BACKEND_DRIVER="artifacts/backend_selfhost_self_obj/cheng.stage2"
  fi
fi

driver="$(sh src/tooling/backend_driver_path.sh)"

objdump=""
if command -v xcrun >/dev/null 2>&1; then
  objdump="$(xcrun --find llvm-objdump 2>/dev/null || true)"
fi
if [ "$objdump" = "" ] && command -v llvm-objdump >/dev/null 2>&1; then
  objdump="$(command -v llvm-objdump)"
fi

if [ "$objdump" = "" ]; then
  echo "[verify_backend_self_linker_riscv64] skip: missing llvm-objdump" >&2
  exit 2
fi

out_dir="artifacts/backend_self_linker_riscv64"
mkdir -p "$out_dir"

target="${BACKEND_RISCV64_TARGET:-riscv64-unknown-linux-gnu}"

runtime_src="src/std/system_helpers_backend.cheng"
runtime_obj="$out_dir/system_helpers_backend.$target.o"

if [ ! -f "$runtime_obj" ] || [ "$runtime_src" -nt "$runtime_obj" ]; then
  MM="$mm" \
  BACKEND_ALLOW_NO_MAIN=1 \
  BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_EMIT=obj \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_INPUT="$runtime_src" \
  BACKEND_OUTPUT="$runtime_obj" \
  "$driver" >/dev/null
fi

if [ ! -s "$runtime_obj" ]; then
  echo "[verify_backend_self_linker_riscv64] missing runtime obj: $runtime_obj" >&2
  exit 1
fi

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
exe_path="$out_dir/hello_puts.$target"

BACKEND_LINKER=self \
BACKEND_RUNTIME_OBJ="$runtime_obj" \
BACKEND_NO_RUNTIME_C=1 \
MM="$mm" \
BACKEND_EMIT=exe \
BACKEND_TARGET="$target" \
BACKEND_FRONTEND=stage1 \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$exe_path" \
  "$driver" >/dev/null

if [ ! -s "$exe_path" ]; then
  echo "[verify_backend_self_linker_riscv64] missing output: $exe_path" >&2
  exit 1
fi

"$objdump" --private-headers "$exe_path" > "$out_dir/hello_puts.$target.objdump.private.txt"

grep -qi "file format elf64" "$out_dir/hello_puts.$target.objdump.private.txt" || {
  echo "[verify_backend_self_linker_riscv64] unexpected file format: $exe_path" >&2
  exit 1
}
grep -qi "riscv" "$out_dir/hello_puts.$target.objdump.private.txt" || {
  echo "[verify_backend_self_linker_riscv64] unexpected arch: $exe_path" >&2
  exit 1
}
grep -q "INTERP" "$out_dir/hello_puts.$target.objdump.private.txt"
grep -q "DYNAMIC" "$out_dir/hello_puts.$target.objdump.private.txt"

grep -q "Dynamic Section:" "$out_dir/hello_puts.$target.objdump.private.txt"
grep -q "NEEDED" "$out_dir/hello_puts.$target.objdump.private.txt"
grep -q "STRTAB" "$out_dir/hello_puts.$target.objdump.private.txt"
grep -q "SYMTAB" "$out_dir/hello_puts.$target.objdump.private.txt"
grep -q "RELA" "$out_dir/hello_puts.$target.objdump.private.txt"

echo "verify_backend_self_linker_riscv64 ok"
