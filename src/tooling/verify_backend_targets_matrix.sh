#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_backend_mvp_driver_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"

strict="${CHENG_BACKEND_MATRIX_STRICT:-0}"

fail_or_warn() {
  msg="$1"
  if [ "$strict" = "1" ]; then
    echo "[verify_backend_targets_matrix] $msg" 1>&2
    exit 1
  fi
  echo "[verify_backend_targets_matrix] (skip) $msg" 1>&2
}

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

pick_nm_tool_for() {
  obj="$1"
  if command -v nm >/dev/null 2>&1 && nm "$obj" >/dev/null 2>&1; then
    printf "%s\n" "nm"
    return 0
  fi
  for n in llvm-nm llvm-nm-19 llvm-nm-18 llvm-nm-17 llvm-nm-16 llvm-nm-15 llvm-nm-14; do
    if command -v "$n" >/dev/null 2>&1 && "$n" "$obj" >/dev/null 2>&1; then
      command -v "$n"
      return 0
    fi
  done
  return 1
}

llvm_objdump="$(find_llvm_objdump || true)"

out_dir="artifacts/backend_targets_matrix"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"

magic_hex() {
  # Prints first 4 bytes as lowercase hex.
  od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n'
}

magic_hex2() {
  # Prints first 2 bytes as lowercase hex.
  od -An -tx1 -N2 "$1" 2>/dev/null | tr -d ' \n'
}

darwin_obj="$out_dir/hello_importc_puts.darwin.o"
darwin_x64_obj="$out_dir/hello_importc_puts.darwin_x86_64.o"
ios_obj="$out_dir/hello_importc_puts.ios.o"
android_obj="$out_dir/hello_importc_puts.android.o"
linux_x64_obj="$out_dir/hello_importc_puts.linux_x86_64.o"
windows_obj="$out_dir/hello_importc_puts.windows.obj"
windows_x64_obj="$out_dir/hello_importc_puts.windows_x86_64.obj"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=arm64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$darwin_obj" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=x86_64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$darwin_x64_obj" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=arm64-apple-ios \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$ios_obj" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=aarch64-linux-android \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$android_obj" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=x86_64-unknown-linux-gnu \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$linux_x64_obj" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=aarch64-pc-windows-msvc \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$windows_obj" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=x86_64-pc-windows-msvc \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$windows_x64_obj" \
"$driver"

[ "$(magic_hex "$darwin_obj")" = "cffaedfe" ] || fail_or_warn "unexpected darwin object magic: $darwin_obj"
darwin_nm="$(pick_nm_tool_for "$darwin_obj" || true)"
if [ "$darwin_nm" != "" ]; then
  "$darwin_nm" "$darwin_obj" | grep -Eq " T _?main$" || fail_or_warn "missing _main/main in darwin object: $darwin_obj"
  "$darwin_nm" "$darwin_obj" | grep -Eq " U _?puts$" || fail_or_warn "missing _puts/puts in darwin object: $darwin_obj"
else
  fail_or_warn "missing nm/llvm-nm (required to validate darwin object symbols): $darwin_obj"
fi
if command -v otool >/dev/null 2>&1; then
  otool -lv "$darwin_obj" | grep -q "sectname __cstring" || fail_or_warn "missing __cstring section in darwin object: $darwin_obj"
elif [ "$llvm_objdump" != "" ]; then
  "$llvm_objdump" -h "$darwin_obj" > "$out_dir/hello_importc_puts.darwin.objdump.h.txt" || fail_or_warn "llvm-objdump failed for darwin object: $darwin_obj"
  grep -q "__cstring" "$out_dir/hello_importc_puts.darwin.objdump.h.txt" || fail_or_warn "missing __cstring section in darwin object: $darwin_obj"
else
  fail_or_warn "missing otool/llvm-objdump (required to validate darwin object sections): $darwin_obj"
fi

[ "$(magic_hex "$darwin_x64_obj")" = "cffaedfe" ] || fail_or_warn "unexpected darwin x86_64 object magic: $darwin_x64_obj"
darwin_x64_nm="$(pick_nm_tool_for "$darwin_x64_obj" || true)"
if [ "$darwin_x64_nm" != "" ]; then
  "$darwin_x64_nm" "$darwin_x64_obj" | grep -Eq " T _?main$" || fail_or_warn "missing _main/main in darwin x86_64 object: $darwin_x64_obj"
  "$darwin_x64_nm" "$darwin_x64_obj" | grep -Eq " U _?puts$" || fail_or_warn "missing _puts/puts in darwin x86_64 object: $darwin_x64_obj"
else
  fail_or_warn "missing nm/llvm-nm (required to validate darwin x86_64 object symbols): $darwin_x64_obj"
fi
if command -v otool >/dev/null 2>&1; then
  otool -hv "$darwin_x64_obj" | grep -q "X86_64" || fail_or_warn "unexpected darwin x86_64 object header: $darwin_x64_obj"
  otool -lv "$darwin_x64_obj" | grep -q "sectname __cstring" || fail_or_warn "missing __cstring section in darwin x86_64 object: $darwin_x64_obj"
elif [ "$llvm_objdump" != "" ]; then
  "$llvm_objdump" -h "$darwin_x64_obj" > "$out_dir/hello_importc_puts.darwin_x86_64.objdump.h.txt" || fail_or_warn "llvm-objdump failed for darwin x86_64 object: $darwin_x64_obj"
  grep -Eq "mach-o.*x86-64|x86-64" "$out_dir/hello_importc_puts.darwin_x86_64.objdump.h.txt" || fail_or_warn "unexpected darwin x86_64 object header: $darwin_x64_obj"
  grep -q "__cstring" "$out_dir/hello_importc_puts.darwin_x86_64.objdump.h.txt" || fail_or_warn "missing __cstring section in darwin x86_64 object: $darwin_x64_obj"
else
  fail_or_warn "missing otool/llvm-objdump (required to validate darwin x86_64 object sections): $darwin_x64_obj"
fi

[ "$(magic_hex "$ios_obj")" = "cffaedfe" ] || fail_or_warn "unexpected ios object magic: $ios_obj"
ios_nm="$(pick_nm_tool_for "$ios_obj" || true)"
if [ "$ios_nm" != "" ]; then
  "$ios_nm" "$ios_obj" | grep -Eq " T _?main$" || fail_or_warn "missing _main/main in ios object: $ios_obj"
  "$ios_nm" "$ios_obj" | grep -Eq " U _?puts$" || fail_or_warn "missing _puts/puts in ios object: $ios_obj"
else
  fail_or_warn "missing nm/llvm-nm (required to validate ios object symbols): $ios_obj"
fi

[ "$(magic_hex "$android_obj")" = "7f454c46" ] || fail_or_warn "unexpected android object magic: $android_obj"
strings "$android_obj" | grep -q "main" || fail_or_warn "missing main in android object: $android_obj"
strings "$android_obj" | grep -q "puts" || fail_or_warn "missing puts in android object: $android_obj"
strings "$android_obj" | grep -q "\\.rodata" || fail_or_warn "missing .rodata in android object: $android_obj"

[ "$(magic_hex "$linux_x64_obj")" = "7f454c46" ] || fail_or_warn "unexpected linux x86_64 object magic: $linux_x64_obj"
strings "$linux_x64_obj" | grep -q "main" || fail_or_warn "missing main in linux x86_64 object: $linux_x64_obj"
strings "$linux_x64_obj" | grep -q "puts" || fail_or_warn "missing puts in linux x86_64 object: $linux_x64_obj"
strings "$linux_x64_obj" | grep -q "\\.rodata" || fail_or_warn "missing .rodata in linux x86_64 object: $linux_x64_obj"
if [ "$llvm_objdump" != "" ]; then
  "$llvm_objdump" -r -d "$linux_x64_obj" > "$out_dir/hello_importc_puts.linux_x86_64.objdump.rd.txt"
  grep -q "file format elf64-x86-64" "$out_dir/hello_importc_puts.linux_x86_64.objdump.rd.txt" || fail_or_warn "unexpected llvm-objdump format for linux x86_64 object: $linux_x64_obj"
  grep -q "R_X86_64_PC32" "$out_dir/hello_importc_puts.linux_x86_64.objdump.rd.txt" || fail_or_warn "missing R_X86_64_PC32 relocation in linux x86_64 object: $linux_x64_obj"
  grep -q "R_X86_64_PLT32" "$out_dir/hello_importc_puts.linux_x86_64.objdump.rd.txt" || fail_or_warn "missing R_X86_64_PLT32 relocation in linux x86_64 object: $linux_x64_obj"
  grep -q "puts-0x4" "$out_dir/hello_importc_puts.linux_x86_64.objdump.rd.txt" || fail_or_warn "missing ELF x86_64 call addend (-0x4) in linux object: $linux_x64_obj"
else
  fail_or_warn "missing llvm-objdump to validate linux x86_64 relocs: $linux_x64_obj"
fi

# COFF machine AArch64 is 0xaa64 (little-endian: 64 aa).
[ "$(magic_hex2 "$windows_obj")" = "64aa" ] || fail_or_warn "unexpected windows COFF machine: $windows_obj"
strings "$windows_obj" | grep -q "main" || fail_or_warn "missing main in windows object: $windows_obj"
strings "$windows_obj" | grep -q "puts" || fail_or_warn "missing puts in windows object: $windows_obj"
strings "$windows_obj" | grep -q "\\.rdata" || fail_or_warn "missing .rdata in windows object: $windows_obj"

# COFF machine x86_64 is 0x8664 (little-endian: 64 86).
[ "$(magic_hex2 "$windows_x64_obj")" = "6486" ] || fail_or_warn "unexpected windows x86_64 COFF machine: $windows_x64_obj"
strings "$windows_x64_obj" | grep -q "main" || fail_or_warn "missing main in windows x86_64 object: $windows_x64_obj"
strings "$windows_x64_obj" | grep -q "puts" || fail_or_warn "missing puts in windows x86_64 object: $windows_x64_obj"
strings "$windows_x64_obj" | grep -q "\\.rdata" || fail_or_warn "missing .rdata in windows x86_64 object: $windows_x64_obj"

echo "verify_backend_targets_matrix ok"
