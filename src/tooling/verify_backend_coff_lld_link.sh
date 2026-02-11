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

find_objdump() {
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

objdump="$(find_objdump || true)"
if [ "$objdump" = "" ]; then
  echo "[verify_backend_coff_lld_link] missing llvm-objdump" >&2
  exit 2
fi

lld_kind=""
lld_cmd=""
if command -v lld-link >/dev/null 2>&1; then
  lld_kind="lld-link"
  lld_cmd="lld-link"
elif command -v llvm-lld >/dev/null 2>&1; then
  lld_kind="llvm-lld"
  lld_cmd="llvm-lld -flavor link"
elif command -v ld.lld >/dev/null 2>&1; then
  lld_kind="ld.lld"
  lld_cmd="ld.lld -flavor link"
elif command -v lld >/dev/null 2>&1; then
  lld_kind="lld"
  lld_cmd="lld -flavor link"
else
  echo "[verify_backend_coff_lld_link] missing lld-link/llvm-lld/ld.lld/lld (install LLVM to enable)" >&2
  exit 2
fi


out_dir="artifacts/backend_coff_lld_link"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"
obj_path="$out_dir/return_add.self.coff.arm64.obj"
dll_path="$out_dir/return_add.self.coff.arm64.dll"
obj_path_x64="$out_dir/return_add.self.coff.x86_64.obj"
dll_path_x64="$out_dir/return_add.self.coff.x86_64.dll"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_OBJ_WRITER=coff \
CHENG_BACKEND_TARGET=aarch64-pc-windows-msvc \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$obj_path" \
"$driver"

if [ ! -s "$obj_path" ]; then
  echo "[verify_backend_coff_lld_link] missing output: $obj_path" >&2
  exit 1
fi

# Link a DLL without CRT/system libs (best-effort); exports `main` for symbol presence checks.
if [ -f "$dll_path" ]; then mv "$dll_path" "$dll_path.prev.$$" 2>/dev/null || true; fi
if [ -f "$dll_path.lib" ]; then mv "$dll_path.lib" "$dll_path.lib.prev.$$" 2>/dev/null || true; fi
sh -c "$lld_cmd /DLL /NOENTRY /NOIMPLIB /OUT:$dll_path /EXPORT:main $obj_path"

if [ ! -s "$dll_path" ]; then
  echo "[verify_backend_coff_lld_link] missing output: $dll_path" >&2
  exit 1
fi

"$objdump" -h "$dll_path" > "$out_dir/return_add.self.coff.dll.objdump.h.txt"
grep -q "coff-arm64" "$out_dir/return_add.self.coff.dll.objdump.h.txt"
grep -q "\\.text" "$out_dir/return_add.self.coff.dll.objdump.h.txt"

"$objdump" -t "$dll_path" > "$out_dir/return_add.self.coff.dll.objdump.t.txt" || true
grep -q "main" "$out_dir/return_add.self.coff.dll.objdump.t.txt" || {
  echo "[verify_backend_coff_lld_link] missing symbol main in dll (lld=$lld_kind)" >&2
  exit 1
}

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_OBJ_WRITER=coff \
CHENG_BACKEND_TARGET=x86_64-pc-windows-msvc \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$obj_path_x64" \
"$driver"

if [ ! -s "$obj_path_x64" ]; then
  echo "[verify_backend_coff_lld_link] missing output: $obj_path_x64" >&2
  exit 1
fi

if [ -f "$dll_path_x64" ]; then mv "$dll_path_x64" "$dll_path_x64.prev.$$" 2>/dev/null || true; fi
if [ -f "$dll_path_x64.lib" ]; then mv "$dll_path_x64.lib" "$dll_path_x64.lib.prev.$$" 2>/dev/null || true; fi
sh -c "$lld_cmd /DLL /NOENTRY /NOIMPLIB /OUT:$dll_path_x64 /EXPORT:main $obj_path_x64"

if [ ! -s "$dll_path_x64" ]; then
  echo "[verify_backend_coff_lld_link] missing output: $dll_path_x64" >&2
  exit 1
fi

"$objdump" -h "$dll_path_x64" > "$out_dir/return_add.self.coff.x86_64.dll.objdump.h.txt"
grep -q "coff-x86-64" "$out_dir/return_add.self.coff.x86_64.dll.objdump.h.txt"
grep -q "\\.text" "$out_dir/return_add.self.coff.x86_64.dll.objdump.h.txt"

"$objdump" -t "$dll_path_x64" > "$out_dir/return_add.self.coff.x86_64.dll.objdump.t.txt" || true
grep -q "main" "$out_dir/return_add.self.coff.x86_64.dll.objdump.t.txt" || {
  echo "[verify_backend_coff_lld_link] missing symbol main in x86_64 dll (lld=$lld_kind)" >&2
  exit 1
}

echo "verify_backend_coff_lld_link ok (lld=$lld_kind)"
