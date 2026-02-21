#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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

strict="${BACKEND_MATRIX_STRICT:-0}"

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

has_macho_cstring_section() {
  obj="$1"
  if command -v otool >/dev/null 2>&1; then
    if otool -lv "$obj" | grep -q "sectname __cstring"; then
      return 0
    fi
    return 1
  fi
  if [ "$llvm_objdump" != "" ]; then
    if "$llvm_objdump" -h "$obj" | grep -q "__cstring"; then
      return 0
    fi
    return 1
  fi
  return 2
}

has_cheng_str_symbol() {
  obj="$1"
  nm_tool="$2"
  [ "$nm_tool" != "" ] || return 1
  "$nm_tool" "$obj" | grep -Eq "L_cheng_str_[0-9a-fA-F]+"
}

llvm_objdump="$(find_llvm_objdump || true)"

out_dir="artifacts/backend_targets_matrix"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
driver="$(sh src/tooling/backend_driver_path.sh)"

is_macho_x86_64_obj() {
  obj="$1"
  if command -v otool >/dev/null 2>&1; then
    otool -hv "$obj" | grep -q "X86_64"
    return $?
  fi
  if [ "$llvm_objdump" != "" ]; then
    "$llvm_objdump" -h "$obj" | grep -Eq "mach-o.*x86-64|x86-64"
    return $?
  fi
  return 2
}

driver_supports_darwin_x86_64() {
  cand="$1"
  [ -x "$cand" ] || return 1
  probe_obj="$out_dir/.driver_probe_x86_64.o"
  probe_log="$out_dir/.driver_probe_x86_64.log"
  rm -f "$probe_obj" "$probe_log"
  set +e
  BACKEND_EMIT=obj \
  BACKEND_TARGET=x86_64-apple-darwin \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$probe_obj" \
  "$cand" >"$probe_log" 2>&1
  rc="$?"
  set -e
  if [ "$rc" -ne 0 ] || [ ! -s "$probe_obj" ]; then
    return 1
  fi
  set +e
  is_macho_x86_64_obj "$probe_obj"
  rc="$?"
  set -e
  [ "$rc" -eq 0 ]
}

host_os="$(uname -s 2>/dev/null || echo unknown)"
if [ "$host_os" = "Darwin" ]; then
  if ! driver_supports_darwin_x86_64 "$driver"; then
    for cand in \
      "$root/dist/releases/current/cheng" \
      "$root/artifacts/backend_selfhost_self_obj/cheng.stage2" \
      "$root/artifacts/backend_seed/cheng.stage2" \
      "$root/artifacts/backend_selfhost_self_obj/cheng.stage1"; do
      [ "$cand" != "$driver" ] || continue
      if driver_supports_darwin_x86_64 "$cand"; then
        echo "[verify_backend_targets_matrix] info: switch matrix driver -> $cand" 1>&2
        driver="$cand"
        break
      fi
    done
  fi
fi

compile_target() {
  target="$1"
  output="$2"
  log="$3"
  set +e
  BACKEND_EMIT=obj \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$output" \
  "$driver" >"$log" 2>&1
  rc="$?"
  set -e
  return "$rc"
}

is_darwin_only_bootstrap_reject() {
  log="$1"
  if [ ! -f "$log" ]; then
    return 1
  fi
  rg -q "uir_codegen: bootstrap path only supports darwin target" "$log"
}

compile_required_target() {
  label="$1"
  target="$2"
  output="$3"
  log="$out_dir/hello_importc_puts.${label}.build.log"
  if compile_target "$target" "$output" "$log"; then
    return 0
  fi
  echo "[verify_backend_targets_matrix] failed to build $label target ($target): $log" 1>&2
  sed -n '1,120p' "$log" 1>&2 || true
  exit 1
}

compile_optional_target() {
  out_var="$1"
  label="$2"
  target="$3"
  output="$4"
  log="$out_dir/hello_importc_puts.${label}.build.log"
  if compile_target "$target" "$output" "$log"; then
    eval "$out_var=1"
    return 0
  fi
  if is_darwin_only_bootstrap_reject "$log"; then
    eval "$out_var=0"
    echo "[verify_backend_targets_matrix] skip ${label} target (${target}): bootstrap darwin-only path" 1>&2
    return 0
  fi
  echo "[verify_backend_targets_matrix] failed to build $label target ($target): $log" 1>&2
  sed -n '1,120p' "$log" 1>&2 || true
  exit 1
}

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

compile_required_target "darwin" "arm64-apple-darwin" "$darwin_obj"
compile_required_target "darwin_x86_64" "x86_64-apple-darwin" "$darwin_x64_obj"
compile_required_target "ios" "arm64-apple-ios" "$ios_obj"

android_enabled="1"
linux_x64_enabled="1"
windows_enabled="1"
windows_x64_enabled="1"
compile_optional_target android_enabled "android" "aarch64-linux-android" "$android_obj"
compile_optional_target linux_x64_enabled "linux_x86_64" "x86_64-unknown-linux-gnu" "$linux_x64_obj"
compile_optional_target windows_enabled "windows" "aarch64-pc-windows-msvc" "$windows_obj"
compile_optional_target windows_x64_enabled "windows_x86_64" "x86_64-pc-windows-msvc" "$windows_x64_obj"

[ "$(magic_hex "$darwin_obj")" = "cffaedfe" ] || fail_or_warn "unexpected darwin object magic: $darwin_obj"
darwin_nm="$(pick_nm_tool_for "$darwin_obj" || true)"
if [ "$darwin_nm" != "" ]; then
  "$darwin_nm" "$darwin_obj" | grep -Eq " T _?main$" || fail_or_warn "missing _main/main in darwin object: $darwin_obj"
  "$darwin_nm" "$darwin_obj" | grep -Eq " U _?puts$" || fail_or_warn "missing _puts/puts in darwin object: $darwin_obj"
else
  fail_or_warn "missing nm/llvm-nm (required to validate darwin object symbols): $darwin_obj"
fi
darwin_cstring_status=0
set +e
has_macho_cstring_section "$darwin_obj"
darwin_cstring_status="$?"
set -e
if [ "$darwin_cstring_status" -eq 2 ] && [ "$darwin_nm" = "" ]; then
  fail_or_warn "missing otool/llvm-objdump and nm/llvm-nm (required to validate darwin string storage): $darwin_obj"
fi
if [ "$darwin_cstring_status" -ne 0 ] && ! has_cheng_str_symbol "$darwin_obj" "$darwin_nm"; then
  fail_or_warn "missing __cstring section or L_cheng_str_* symbols in darwin object: $darwin_obj"
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
elif [ "$llvm_objdump" != "" ]; then
  "$llvm_objdump" -h "$darwin_x64_obj" > "$out_dir/hello_importc_puts.darwin_x86_64.objdump.h.txt" || fail_or_warn "llvm-objdump failed for darwin x86_64 object: $darwin_x64_obj"
  grep -Eq "mach-o.*x86-64|x86-64" "$out_dir/hello_importc_puts.darwin_x86_64.objdump.h.txt" || fail_or_warn "unexpected darwin x86_64 object header: $darwin_x64_obj"
else
  if [ "$darwin_x64_nm" = "" ]; then
    fail_or_warn "missing otool/llvm-objdump and nm/llvm-nm (required to validate darwin x86_64 string storage): $darwin_x64_obj"
  fi
fi
darwin_x64_cstring_status=0
set +e
has_macho_cstring_section "$darwin_x64_obj"
darwin_x64_cstring_status="$?"
set -e
if [ "$darwin_x64_cstring_status" -eq 2 ] && [ "$darwin_x64_nm" = "" ]; then
  fail_or_warn "missing otool/llvm-objdump and nm/llvm-nm (required to validate darwin x86_64 string storage): $darwin_x64_obj"
fi
if [ "$darwin_x64_cstring_status" -ne 0 ] && ! has_cheng_str_symbol "$darwin_x64_obj" "$darwin_x64_nm"; then
  fail_or_warn "missing __cstring section or L_cheng_str_* symbols in darwin x86_64 object: $darwin_x64_obj"
fi

[ "$(magic_hex "$ios_obj")" = "cffaedfe" ] || fail_or_warn "unexpected ios object magic: $ios_obj"
ios_nm="$(pick_nm_tool_for "$ios_obj" || true)"
if [ "$ios_nm" != "" ]; then
  "$ios_nm" "$ios_obj" | grep -Eq " T _?main$" || fail_or_warn "missing _main/main in ios object: $ios_obj"
  "$ios_nm" "$ios_obj" | grep -Eq " U _?puts$" || fail_or_warn "missing _puts/puts in ios object: $ios_obj"
else
  fail_or_warn "missing nm/llvm-nm (required to validate ios object symbols): $ios_obj"
fi

if [ "$android_enabled" = "1" ]; then
  [ "$(magic_hex "$android_obj")" = "7f454c46" ] || fail_or_warn "unexpected android object magic: $android_obj"
  strings "$android_obj" | grep -q "main" || fail_or_warn "missing main in android object: $android_obj"
  strings "$android_obj" | grep -q "puts" || fail_or_warn "missing puts in android object: $android_obj"
  strings "$android_obj" | grep -q "\\.rodata" || fail_or_warn "missing .rodata in android object: $android_obj"
fi

if [ "$linux_x64_enabled" = "1" ]; then
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
fi

if [ "$windows_enabled" = "1" ]; then
  # COFF machine AArch64 is 0xaa64 (little-endian: 64 aa).
  [ "$(magic_hex2 "$windows_obj")" = "64aa" ] || fail_or_warn "unexpected windows COFF machine: $windows_obj"
  strings "$windows_obj" | grep -q "main" || fail_or_warn "missing main in windows object: $windows_obj"
  strings "$windows_obj" | grep -q "puts" || fail_or_warn "missing puts in windows object: $windows_obj"
  strings "$windows_obj" | grep -q "\\.rdata" || fail_or_warn "missing .rdata in windows object: $windows_obj"
fi

if [ "$windows_x64_enabled" = "1" ]; then
  # COFF machine x86_64 is 0x8664 (little-endian: 64 86).
  [ "$(magic_hex2 "$windows_x64_obj")" = "6486" ] || fail_or_warn "unexpected windows x86_64 COFF machine: $windows_x64_obj"
  strings "$windows_x64_obj" | grep -q "main" || fail_or_warn "missing main in windows x86_64 object: $windows_x64_obj"
  strings "$windows_x64_obj" | grep -q "puts" || fail_or_warn "missing puts in windows x86_64 object: $windows_x64_obj"
  strings "$windows_x64_obj" | grep -q "\\.rdata" || fail_or_warn "missing .rdata in windows x86_64 object: $windows_x64_obj"
fi

echo "verify_backend_targets_matrix ok"
