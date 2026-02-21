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

bool01_from_pattern() {
  pattern="$1"
  file="$2"
  if grep -Eq "$pattern" "$file"; then
    printf "1\n"
  else
    printf "0\n"
  fi
}

magic_hex() {
  od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n'
}

detect_format() {
  bin="$1"
  case "$(magic_hex "$bin")" in
    cffaedfe*) printf "mach-o64\n" ;;
    7f454c46*) printf "elf64\n" ;;
    4d5a*) printf "pe64\n" ;;
    *) printf "unknown\n" ;;
  esac
}

write_manifest() {
  platform="$1"
  binary="$2"
  meta_file="$3"
  nm_tool="$4"
  out="$5"
  : > "$out"
  fmt="$(detect_format "$binary")"
  main_pat='[[:space:]]+[Tt][[:space:]]+_?main$'
  puts_pat='[[:space:]]+[Uu][[:space:]]+_?puts(@.*)?$'
  # Feature detection is string-based for cross-host portability and is also
  # used as a fallback when host nm tooling cannot decode the target binary.
  strings "$binary" >"$out.strings.txt" 2>/dev/null || true
  if [ "$nm_tool" != "" ] && "$nm_tool" "$binary" >"$out.nm.txt" 2>/dev/null && [ -s "$out.nm.txt" ]; then
    main01="$(bool01_from_pattern "$main_pat" "$out.nm.txt")"
    puts01="$(bool01_from_pattern "$puts_pat" "$out.nm.txt")"
  else
    main01="$(bool01_from_pattern '(^|[^A-Za-z0-9_])_?main([^A-Za-z0-9_]|$)' "$out.strings.txt")"
    puts01="$(bool01_from_pattern '(^|[^A-Za-z0-9_])_?puts([^A-Za-z0-9_]|$)' "$out.strings.txt")"
  fi
  tls01="$(bool01_from_pattern '(\.tdata|\.tbss|__thread|TLS)' "$out.strings.txt")"
  eh01="$(bool01_from_pattern '(eh_frame|__eh_frame)' "$out.strings.txt")"
  dwarf01="$(bool01_from_pattern '(\.debug_|__DWARF|DWARF)' "$out.strings.txt")"

  if [ "$platform" = "darwin" ]; then
    symtab01="$(bool01_from_pattern 'LC_SYMTAB' "$meta_file")"
    interp01="$(bool01_from_pattern 'LC_LOAD_DYLINKER' "$meta_file")"
    dynamic01="$(bool01_from_pattern 'LC_DYLD_INFO_ONLY' "$meta_file")"
  elif [ "$platform" = "windows" ]; then
    symtab01="$(bool01_from_pattern '(Symbol Table|Number of Symbols)' "$meta_file")"
    interp01="0"
    dynamic01="$(bool01_from_pattern 'DLL Name:' "$meta_file")"
  else
    symtab01="$(bool01_from_pattern 'SYMTAB' "$meta_file")"
    interp01="$(bool01_from_pattern 'INTERP' "$meta_file")"
    dynamic01="$(bool01_from_pattern 'DYNAMIC' "$meta_file")"
  fi

  printf "platform.os=%s\n" "$platform" >>"$out"
  printf "binary.format=%s\n" "$fmt" >>"$out"
  printf "linker.mode=self\n" >>"$out"
  printf "entry.main=%s\n" "$main01" >>"$out"
  printf "import.puts=%s\n" "$puts01" >>"$out"
  printf "meta.symtab=%s\n" "$symtab01" >>"$out"
  printf "meta.interp=%s\n" "$interp01" >>"$out"
  printf "meta.dynamic=%s\n" "$dynamic01" >>"$out"
  printf "feature.tls=%s\n" "$tls01" >>"$out"
  printf "feature.eh_frame=%s\n" "$eh01" >>"$out"
  printf "feature.dwarf=%s\n" "$dwarf01" >>"$out"
}

manifest_value() {
  key="$1"
  file="$2"
  awk -F= -v k="$key" '$1 == k { print substr($0, index($0, "=") + 1); found=1; exit } END { if (!found) print "" }' "$file"
}

out_dir="artifacts/backend_linker_abi_core"
rm -rf "$out_dir"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
driver_probe_fixture="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$driver_probe_fixture" ]; then
  driver_probe_fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
darwin_target="arm64-apple-darwin"
linux_target="aarch64-unknown-linux-gnu"
windows_target="aarch64-pc-windows-msvc"
darwin_exe="$out_dir/hello_importc_puts.$darwin_target.self"
linux_exe="$out_dir/hello_importc_puts.$linux_target.self"
windows_exe="$out_dir/hello_importc_puts.$windows_target.self"
darwin_log="$out_dir/build.darwin.log"
linux_log="$out_dir/build.linux.log"
windows_log="$out_dir/build.windows.log"

to_abs() {
  p="$1"
  case "$p" in
    /*) ;;
    *) p="$root/$p" ;;
  esac
  d="$(CDPATH= cd -- "$(dirname -- "$p")" && pwd 2>/dev/null || dirname -- "$p")"
  printf "%s/%s\n" "$d" "$(basename -- "$p")"
}

driver_help_ok() {
  bin="$1"
  set +e
  "$bin" --help >/dev/null 2>&1
  status="$?"
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

probe_driver_target() {
  bin="$1"
  target="$2"
  out="$3"
  log="$4"
  is_crash_status() {
    code="$1"
    case "$code" in
      132|133|134|135|136|137|139)
        return 0
        ;;
    esac
    return 1
  }
  cleanup_probe_sidecar() {
    p="$1"
    p_base="${p%.*}"
    for q in "$p" "$p_base"; do
      rm -f "$q.o" "$q.tmp" "$q.tmp.linkobj"
      rm -rf "$q.objs" "$q.objs.lock"
    done
  }
  cleanup_probe_sidecar "$out"
  set +e
  env \
    ABI=v2_noptr \
    CACHE=0 \
    BACKEND_DRIVER="$bin" \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM=1 \
    STAGE1_SKIP_OWNERSHIP=1 \
    BACKEND_LINKER=self \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_JOBS=1 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$driver_probe_fixture" \
    BACKEND_OUTPUT="$out" \
    "$bin" >"$log" 2>&1
  status="$?"
  if [ "$status" -ne 0 ] && is_crash_status "$status"; then
    cleanup_probe_sidecar "$out"
    env \
      ABI=v2_noptr \
      CACHE=0 \
      BACKEND_DRIVER="$bin" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM=1 \
      STAGE1_SKIP_OWNERSHIP=1 \
      BACKEND_LINKER=self \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_INCREMENTAL=0 \
      BACKEND_JOBS=1 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$driver_probe_fixture" \
      BACKEND_OUTPUT="$out" \
      "$bin" >"$log" 2>&1
    status="$?"
  fi
  set -e
  cleanup_probe_sidecar "$out"
  if [ "$status" -ne 0 ] || [ ! -s "$out" ]; then
    return 1
  fi
  return 0
}

pick_driver_for_linker_abi_core() {
  allow_selfhost_fallback="${BACKEND_LINKER_ABI_CORE_ALLOW_SELFHOST:-0}"
  path_from_resolver="$(BACKEND_DRIVER_PATH_ALLOW_SELFHOST=0 sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
  for cand in \
    "${BACKEND_LINKER_ABI_CORE_DRIVER:-}" \
    "${BACKEND_DRIVER:-}" \
    "artifacts/backend_driver/cheng.fixed3" \
    "artifacts/backend_driver/cheng" \
    "artifacts/backend_seed/cheng.stage2" \
    "dist/releases/current/cheng" \
    "$path_from_resolver"; do
    [ "$cand" != "" ] || continue
    abs="$(to_abs "$cand")"
    [ -x "$abs" ] || continue
    if ! driver_help_ok "$abs"; then
      continue
    fi
    safe="$(printf '%s' "$abs" | tr -c 'A-Za-z0-9._-' '_')"
    probe_darwin_out="$out_dir/driver_probe.${safe}.darwin.self"
    probe_linux_out="$out_dir/driver_probe.${safe}.linux.self"
    probe_darwin_log="$out_dir/driver_probe.${safe}.darwin.log"
    probe_linux_log="$out_dir/driver_probe.${safe}.linux.log"
    probe_windows_out="$out_dir/driver_probe.${safe}.windows.self"
    probe_windows_log="$out_dir/driver_probe.${safe}.windows.log"
    if probe_driver_target "$abs" "$darwin_target" "$probe_darwin_out" "$probe_darwin_log" &&
       probe_driver_target "$abs" "$linux_target" "$probe_linux_out" "$probe_linux_log" &&
       probe_driver_target "$abs" "$windows_target" "$probe_windows_out" "$probe_windows_log"; then
      printf "%s\n" "$abs"
      return 0
    fi
  done
  if [ "$allow_selfhost_fallback" != "1" ]; then
    return 1
  fi
  for cand in \
    "artifacts/backend_selfhost_self_obj/cheng_stage0_prod" \
    "artifacts/backend_selfhost_self_obj/cheng_stage0_default" \
    "artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "artifacts/backend_selfhost_self_obj/cheng.stage1"; do
    [ "$cand" != "" ] || continue
    abs="$(to_abs "$cand")"
    [ -x "$abs" ] || continue
    if ! driver_help_ok "$abs"; then
      continue
    fi
    safe="$(printf '%s' "$abs" | tr -c 'A-Za-z0-9._-' '_')"
    probe_darwin_out="$out_dir/driver_probe.${safe}.darwin.self"
    probe_linux_out="$out_dir/driver_probe.${safe}.linux.self"
    probe_darwin_log="$out_dir/driver_probe.${safe}.darwin.log"
    probe_linux_log="$out_dir/driver_probe.${safe}.linux.log"
    probe_windows_out="$out_dir/driver_probe.${safe}.windows.self"
    probe_windows_log="$out_dir/driver_probe.${safe}.windows.log"
    if probe_driver_target "$abs" "$darwin_target" "$probe_darwin_out" "$probe_darwin_log" &&
       probe_driver_target "$abs" "$linux_target" "$probe_linux_out" "$probe_linux_log" &&
       probe_driver_target "$abs" "$windows_target" "$probe_windows_out" "$probe_windows_log"; then
      printf "%s\n" "$abs"
      return 0
    fi
  done
  return 1
}

driver="$(pick_driver_for_linker_abi_core || true)"
if [ "$driver" = "" ] || [ ! -x "$driver" ]; then
  echo "[verify_backend_linker_abi_core] no driver can self-link darwin/linux/windows targets" >&2
  echo "  hint: set BACKEND_LINKER_ABI_CORE_DRIVER=<path>" >&2
  exit 1
fi

echo "[verify_backend_linker_abi_core] driver=$driver"
echo "[verify_backend_linker_abi_core] fixture=$fixture"
echo "[verify_backend_linker_abi_core] targets=$darwin_target,$linux_target,$windows_target"

build_self_exe() {
  target="$1"
  out="$2"
  log="$3"
  is_crash_status() {
    code="$1"
    case "$code" in
      132|133|134|135|136|137|139)
        return 0
        ;;
    esac
    return 1
  }
  out_base="${out%.*}"
  rm -f "$out" "$out.o" "$out.tmp" "$out.tmp.linkobj" "$out_base.o" "$out_base.tmp" "$out_base.tmp.linkobj"
  rm -rf "$out.objs" "$out.objs.lock" "$out_base.objs" "$out_base.objs.lock"
  set +e
  env \
    ABI=v2_noptr \
    CACHE=0 \
    BACKEND_DRIVER="$driver" \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM=0 \
    STAGE1_SKIP_OWNERSHIP=1 \
    BACKEND_LINKER=self \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_JOBS=1 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$out" \
    "$driver" >"$log" 2>&1
  status="$?"
  if [ "$status" -ne 0 ] && is_crash_status "$status"; then
    rm -f "$out" "$out.o" "$out.tmp" "$out.tmp.linkobj" "$out_base.o" "$out_base.tmp" "$out_base.tmp.linkobj"
    rm -rf "$out.objs" "$out.objs.lock" "$out_base.objs" "$out_base.objs.lock"
    env \
      ABI=v2_noptr \
      CACHE=0 \
      BACKEND_DRIVER="$driver" \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM=0 \
      STAGE1_SKIP_OWNERSHIP=1 \
      BACKEND_LINKER=self \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_INCREMENTAL=0 \
      BACKEND_JOBS=1 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out" \
      "$driver" >"$log" 2>&1
    status="$?"
  fi
  set -e
  rm -f "$out.o" "$out.tmp" "$out.tmp.linkobj" "$out_base.o" "$out_base.tmp" "$out_base.tmp.linkobj"
  rm -rf "$out.objs" "$out.objs.lock" "$out_base.objs" "$out_base.objs.lock"
  return "$status"
}

if ! build_self_exe "$darwin_target" "$darwin_exe" "$darwin_log"; then
  echo "[verify_backend_linker_abi_core] darwin self-link failed" >&2
  tail -n 60 "$darwin_log" >&2 || true
  exit 1
fi

if ! build_self_exe "$linux_target" "$linux_exe" "$linux_log"; then
  echo "[verify_backend_linker_abi_core] linux self-link failed" >&2
  tail -n 60 "$linux_log" >&2 || true
  exit 1
fi

if ! build_self_exe "$windows_target" "$windows_exe" "$windows_log"; then
  echo "[verify_backend_linker_abi_core] windows self-link failed" >&2
  tail -n 60 "$windows_log" >&2 || true
  exit 1
fi

if [ ! -s "$darwin_exe" ] || [ ! -s "$linux_exe" ] || [ ! -s "$windows_exe" ]; then
  echo "[verify_backend_linker_abi_core] missing linker outputs" >&2
  exit 1
fi

llvm_objdump="$(find_llvm_objdump || true)"
if command -v otool >/dev/null 2>&1; then
  otool -l "$darwin_exe" > "$out_dir/darwin.meta.txt"
elif [ "$llvm_objdump" != "" ]; then
  "$llvm_objdump" --macho --private-headers "$darwin_exe" > "$out_dir/darwin.meta.txt"
else
  echo "[verify_backend_linker_abi_core] missing otool/llvm-objdump for darwin metadata" >&2
  exit 1
fi

if [ "$llvm_objdump" != "" ]; then
  "$llvm_objdump" --private-headers "$linux_exe" > "$out_dir/linux.meta.txt"
elif command -v readelf >/dev/null 2>&1; then
  readelf -l -d "$linux_exe" > "$out_dir/linux.meta.txt"
else
  echo "[verify_backend_linker_abi_core] missing llvm-objdump/readelf for linux metadata" >&2
  exit 1
fi

if [ "$llvm_objdump" != "" ]; then
  "$llvm_objdump" --private-headers "$windows_exe" > "$out_dir/windows.meta.txt"
elif command -v objdump >/dev/null 2>&1; then
  objdump -x "$windows_exe" > "$out_dir/windows.meta.txt"
else
  echo "[verify_backend_linker_abi_core] missing llvm-objdump/objdump for windows metadata" >&2
  exit 1
fi

darwin_nm="$(pick_nm_tool_for "$darwin_exe" || true)"
linux_nm="$(pick_nm_tool_for "$linux_exe" || true)"
windows_nm="$(pick_nm_tool_for "$windows_exe" || true)"
if [ "$darwin_nm" = "" ] || [ "$linux_nm" = "" ]; then
  echo "[verify_backend_linker_abi_core] missing nm/llvm-nm for symbol checks" >&2
  exit 1
fi

darwin_manifest="$out_dir/darwin.manifest.txt"
linux_manifest="$out_dir/linux.manifest.txt"
windows_manifest="$out_dir/windows.manifest.txt"
write_manifest "darwin" "$darwin_exe" "$out_dir/darwin.meta.txt" "$darwin_nm" "$darwin_manifest"
write_manifest "linux" "$linux_exe" "$out_dir/linux.meta.txt" "$linux_nm" "$linux_manifest"
write_manifest "windows" "$windows_exe" "$out_dir/windows.meta.txt" "$windows_nm" "$windows_manifest"

LC_ALL=C sort "$darwin_manifest" -o "$darwin_manifest"
LC_ALL=C sort "$linux_manifest" -o "$linux_manifest"
LC_ALL=C sort "$windows_manifest" -o "$windows_manifest"

diff "$darwin_manifest" "$linux_manifest" > "$out_dir/manifest.diff.txt" || true

all_keys_file="$out_dir/manifest.all.keys.txt"
diff_keys_file="$out_dir/manifest.diff.keys.txt"
bad_keys_file="$out_dir/manifest.diff.not_allowed.txt"
allow_file="src/tooling/linker_abi_core_diff_whitelist.allowlist"

(cut -d= -f1 "$darwin_manifest"; cut -d= -f1 "$linux_manifest") | LC_ALL=C sort -u >"$all_keys_file"
: >"$diff_keys_file"
: >"$bad_keys_file"

while IFS= read -r key; do
  [ "$key" != "" ] || continue
  dv="$(manifest_value "$key" "$darwin_manifest")"
  lv="$(manifest_value "$key" "$linux_manifest")"
  if [ "$dv" != "$lv" ]; then
    printf "%s\n" "$key" >>"$diff_keys_file"
  fi
done <"$all_keys_file"

if [ -s "$diff_keys_file" ]; then
  while IFS= read -r key; do
    [ "$key" != "" ] || continue
    if ! grep -Fxq "$key" "$allow_file"; then
      printf "%s\n" "$key" >>"$bad_keys_file"
    fi
  done <"$diff_keys_file"
fi

if [ -s "$bad_keys_file" ]; then
  echo "[verify_backend_linker_abi_core] unexpected darwin/linux manifest diff keys:" >&2
  while IFS= read -r key; do
    [ "$key" != "" ] || continue
    dv="$(manifest_value "$key" "$darwin_manifest")"
    lv="$(manifest_value "$key" "$linux_manifest")"
    echo "  - $key: darwin=$dv linux=$lv" >&2
  done <"$bad_keys_file"
  echo "[verify_backend_linker_abi_core] allowlist: $allow_file" >&2
  exit 1
fi

if [ -s "$diff_keys_file" ]; then
  echo "[verify_backend_linker_abi_core] diff keys (allowlisted):"
  while IFS= read -r key; do
    [ "$key" != "" ] || continue
    dv="$(manifest_value "$key" "$darwin_manifest")"
    lv="$(manifest_value "$key" "$linux_manifest")"
    echo "  - $key: darwin=$dv linux=$lv"
  done <"$diff_keys_file"
fi

if [ "$(manifest_value "binary.format" "$windows_manifest")" != "pe64" ]; then
  echo "[verify_backend_linker_abi_core] windows format check failed: expected pe64" >&2
  exit 1
fi
if [ "$(manifest_value "linker.mode" "$windows_manifest")" != "self" ]; then
  echo "[verify_backend_linker_abi_core] windows linker.mode check failed" >&2
  exit 1
fi
if [ "$(manifest_value "import.puts" "$windows_manifest")" != "1" ]; then
  echo "[verify_backend_linker_abi_core] windows import.puts check failed" >&2
  exit 1
fi
if [ "$(manifest_value "meta.dynamic" "$windows_manifest")" != "1" ]; then
  echo "[verify_backend_linker_abi_core] windows dynamic metadata check failed" >&2
  exit 1
fi
echo "verify_backend_linker_abi_core ok"
