#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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

driver="$(sh src/tooling/backend_driver_path.sh)"

objdump=""
if command -v xcrun >/dev/null 2>&1; then
  objdump="$(xcrun --find llvm-objdump 2>/dev/null || true)"
fi
if [ "$objdump" = "" ] && command -v llvm-objdump >/dev/null 2>&1; then
  objdump="$(command -v llvm-objdump)"
fi
if [ "$objdump" = "" ]; then
  echo "[verify_backend_self_linker_coff] skip: missing llvm-objdump" >&2
  exit 2
fi

out_dir="artifacts/backend_self_linker_coff"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
exe_path="$out_dir/hello_importc_puts.self.coff.exe"

run_build() {
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_COFF_CRT_DLL=UCRTBASE.dll \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_INCREMENTAL=0 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET=aarch64-pc-windows-msvc \
  CHENG_BACKEND_FRONTEND=mvp \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver" >/dev/null
}

if ! run_build; then
  status=$?
  if [ "$status" -eq 134 ]; then
    rm -rf "$exe_path.objs" "$exe_path.objs.lock" 2>/dev/null || true
    run_build
  else
    exit "$status"
  fi
fi

if [ ! -s "$exe_path" ]; then
  echo "[verify_backend_self_linker_coff] missing output: $exe_path" >&2
  exit 1
fi

"$objdump" -h "$exe_path" > "$out_dir/hello_importc_puts.self.coff.exe.objdump.h.txt"
grep -q "file format coff-arm64" "$out_dir/hello_importc_puts.self.coff.exe.objdump.h.txt"
grep -q "\\.text" "$out_dir/hello_importc_puts.self.coff.exe.objdump.h.txt"
grep -q "\\.idata" "$out_dir/hello_importc_puts.self.coff.exe.objdump.h.txt"

"$objdump" --private-headers "$exe_path" > "$out_dir/hello_importc_puts.self.coff.exe.objdump.private.txt"
grep -q "Magic[[:space:]]\\+020b" "$out_dir/hello_importc_puts.self.coff.exe.objdump.private.txt"
grep -q "Subsystem[[:space:]]\\+00000003" "$out_dir/hello_importc_puts.self.coff.exe.objdump.private.txt"
grep -q "DLL Name: KERNEL32\\.dll" "$out_dir/hello_importc_puts.self.coff.exe.objdump.private.txt"
grep -q "ExitProcess" "$out_dir/hello_importc_puts.self.coff.exe.objdump.private.txt"
grep -q "DLL Name: UCRTBASE\\.dll" "$out_dir/hello_importc_puts.self.coff.exe.objdump.private.txt"
grep -q "puts" "$out_dir/hello_importc_puts.self.coff.exe.objdump.private.txt"

echo "verify_backend_self_linker_coff ok"
