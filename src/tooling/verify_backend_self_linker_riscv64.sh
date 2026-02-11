#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

mm="${CHENG_MM:-orc}"

if [ "${CHENG_CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

# Prefer a ready stage2 driver to avoid slow local rebuilds in CI/gates.
if [ "${CHENG_BACKEND_DRIVER:-}" = "" ]; then
  if [ -x "artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
    export CHENG_BACKEND_DRIVER="artifacts/backend_selfhost_self_obj/cheng.stage2"
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

target="${CHENG_BACKEND_RISCV64_TARGET:-riscv64-unknown-linux-gnu}"

runtime_src="src/std/system_helpers_backend.cheng"
runtime_obj="$out_dir/system_helpers_backend.$target.o"

if [ ! -f "$runtime_obj" ] || [ "$runtime_src" -nt "$runtime_obj" ]; then
  CHENG_MM="$mm" \
  CHENG_BACKEND_ALLOW_NO_MAIN=1 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_EMIT=obj \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_FRONTEND=mvp \
  CHENG_BACKEND_INPUT="$runtime_src" \
  CHENG_BACKEND_OUTPUT="$runtime_obj" \
  "$driver" >/dev/null
fi

if [ ! -s "$runtime_obj" ]; then
  echo "[verify_backend_self_linker_riscv64] missing runtime obj: $runtime_obj" >&2
  exit 1
fi

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
exe_path="$out_dir/hello_puts.$target"

CHENG_BACKEND_LINKER=self \
CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
CHENG_BACKEND_NO_RUNTIME_C=1 \
CHENG_MM="$mm" \
CHENG_BACKEND_EMIT=exe \
CHENG_BACKEND_TARGET="$target" \
CHENG_BACKEND_FRONTEND=mvp \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$exe_path" \
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
