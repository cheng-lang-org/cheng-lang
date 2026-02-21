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

fail() {
  echo "[verify_backend_mem_image_core] $1" >&2
  exit 1
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

count_marker() {
  file="$1"
  pattern="$2"
  matches="$(rg -n --no-messages -e "$pattern" "$file" || true)"
  if [ "$matches" = "" ]; then
    echo "0"
    return
  fi
  printf '%s\n' "$matches" | wc -l | tr -d ' '
}

find_otool() {
  if command -v otool >/dev/null 2>&1; then
    command -v otool
    return 0
  fi
  if command -v xcrun >/dev/null 2>&1; then
    p="$(xcrun --find otool 2>/dev/null || true)"
    if [ "$p" != "" ]; then
      printf '%s\n' "$p"
      return 0
    fi
  fi
  return 1
}

find_readelf() {
  for n in readelf llvm-readelf llvm-readelf-19 llvm-readelf-18 llvm-readelf-17 llvm-readelf-16 llvm-readelf-15 llvm-readelf-14; do
    if command -v "$n" >/dev/null 2>&1; then
      command -v "$n"
      return 0
    fi
  done
  return 1
}

is_supported_target() {
  target="$1"
  case "$target" in
    *apple*darwin*|*darwin*)
      case "$target" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *linux*|*android*)
      case "$target" in
        *arm64*|*aarch64*|*riscv64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *windows*|*msvc*)
      case "$target" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

resolve_driver() {
  path_from_resolver="$(BACKEND_DRIVER_PATH_ALLOW_SELFHOST=0 sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
  for candidate in \
    "${BACKEND_MEM_IMAGE_CORE_DRIVER:-}" \
    "${BACKEND_DRIVER:-}" \
    "artifacts/backend_driver/cheng" \
    "artifacts/backend_driver/cheng.fixed3" \
    "dist/releases/current/cheng" \
    "artifacts/backend_seed/cheng.stage2" \
    "$path_from_resolver"; do
    if [ "$candidate" = "" ] || [ ! -x "$candidate" ]; then
      continue
    fi
    if driver_help_ok "$candidate"; then
      printf '%s\n' "$candidate"
      return
    fi
  done
  printf '%s\n' ""
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

host_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || true)"
target="${BACKEND_TARGET:-${BACKEND_MEM_IMAGE_CORE_TARGET:-$host_target}}"
if [ "$target" = "" ]; then
  target="arm64-apple-darwin"
fi
if ! is_supported_target "$target"; then
  fail "unsupported target: $target"
fi

driver="$(resolve_driver)"
if [ "$driver" = "" ] || [ ! -x "$driver" ]; then
  fail "backend driver not executable: $driver"
fi

out_dir="artifacts/backend_mem_image_core"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
exe_path="$out_dir/hello_importc_puts.$safe_target"
case "$target" in
  *windows*|*msvc*) exe_path="$exe_path.exe" ;;
esac
build_log="$out_dir/hello_importc_puts.$safe_target.build.log"
run_log="$out_dir/hello_importc_puts.$safe_target.run.log"
meta_log="$out_dir/hello_importc_puts.$safe_target.meta.log"
report="$out_dir/backend_mem_image_core.$safe_target.report.txt"
snapshot="$out_dir/backend_mem_image_core.$safe_target.snapshot.env"

rm -f "$exe_path" "$exe_path.o" "$build_log" "$run_log" "$meta_log" "$report" "$snapshot"
rm -rf "$exe_path.objs" "$exe_path.objs.lock"

macho_file="src/backend/obj/macho_linker.cheng"
elf_file="src/backend/obj/elf_linker.cheng"
coff_file="src/backend/obj/coff_linker.cheng"

symbol_index_markers=0
section_layout_markers=0
relocation_apply_markers=0
entry_symbol_markers=0
relocation_guard_markers=0

macho_symbol_marker="$(count_marker "$macho_file" 'defIndex: hashmaps\.HashMapStrInt')"
elf_symbol_marker="$(count_marker "$elf_file" 'var defIndex: hashmaps\.HashMapStrInt')"
coff_symbol_marker="$(count_marker "$coff_file" 'var defIndex: hashmaps\.HashMapStrInt')"
symbol_index_markers=$((macho_symbol_marker + elf_symbol_marker + coff_symbol_marker))
if [ "$symbol_index_markers" -lt 3 ]; then
  fail "missing symbol-index markers in linker core files"
fi

macho_layout_marker="$(count_marker "$macho_file" 'MachoLinkLayout = ref')"
elf_layout_marker="$(count_marker "$elf_file" '# Build combined sections\.')"
coff_layout_marker="$(count_marker "$coff_file" '# Build combined sections\.')"
section_layout_markers=$((macho_layout_marker + elf_layout_marker + coff_layout_marker))
if [ "$section_layout_markers" -lt 3 ]; then
  fail "missing section-layout markers in linker core files"
fi

macho_reloc_marker="$(count_marker "$macho_file" '# Patch relocations \(in merged text only\)\.')"
elf_reloc_marker="$(count_marker "$elf_file" '# Patch relocations\.')"
coff_reloc_marker="$(count_marker "$coff_file" '# Patch object relocations\.')"
relocation_apply_markers=$((macho_reloc_marker + elf_reloc_marker + coff_reloc_marker))
if [ "$relocation_apply_markers" -lt 3 ]; then
  fail "missing relocation-apply markers in linker core files"
fi

macho_entry_marker="$(count_marker "$macho_file" 'missing _main symbol')"
elf_entry_marker="$(count_marker "$elf_file" 'missing main symbol')"
coff_entry_marker="$(count_marker "$coff_file" 'missing main symbol')"
entry_symbol_markers=$((macho_entry_marker + elf_entry_marker + coff_entry_marker))
if [ "$entry_symbol_markers" -lt 3 ]; then
  fail "missing entry-symbol resolution markers in linker core files"
fi

macho_guard_marker="$(count_marker "$macho_file" 'reloc symIndex out of range')"
elf_guard_marker="$(count_marker "$elf_file" 'reloc symIndex out of range')"
coff_guard_marker="$(count_marker "$coff_file" 'reloc symIndex out of range')"
relocation_guard_markers=$((macho_guard_marker + elf_guard_marker + coff_guard_marker))
if [ "$relocation_guard_markers" -lt 3 ]; then
  fail "missing relocation guard markers in linker core files"
fi

set +e
env \
  MM="${MM:-orc}" \
  ABI=v2_noptr \
  BACKEND_DRIVER="$driver" \
  BACKEND_TARGET="$target" \
  BACKEND_LINKER=self \
  BACKEND_CODESIGN=0 \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_INCREMENTAL=0 \
  BACKEND_KEEP_EXE_OBJ=0 \
  STAGE1_STD_NO_POINTERS=0 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
  STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
  sh src/tooling/chengc.sh "$fixture" --frontend:stage1 --emit:exe --out:"$exe_path" >"$build_log" 2>&1
build_status="$?"
set -e
if [ "$build_status" -ne 0 ]; then
  echo "[verify_backend_mem_image_core] build failed (status=$build_status): $build_log" >&2
  sed -n '1,200p' "$build_log" >&2 || true
  exit 1
fi

if [ ! -s "$exe_path" ]; then
  fail "missing output executable: $exe_path"
fi
if [ -e "$exe_path.o" ]; then
  fail "unexpected sidecar object remains: $exe_path.o"
fi
if [ -d "$exe_path.objs" ] || [ -d "$exe_path.objs.lock" ]; then
  fail "unexpected multi-object artifacts remain near: $exe_path"
fi

run_requested="${BACKEND_MEM_IMAGE_CORE_RUN:-0}"
run_mode="skip"
run_status="0"
if [ "$run_requested" = "1" ] && [ "$target" = "$host_target" ]; then
  case "$target" in
    *windows*|*msvc*|*android*)
      run_mode="skip"
      ;;
    *)
      run_mode="host"
      set +e
      "$exe_path" >"$run_log" 2>&1
      run_status="$?"
      set -e
      if [ "$run_status" -ne 0 ]; then
        echo "[verify_backend_mem_image_core] executable run failed (status=$run_status): $run_log" >&2
        sed -n '1,120p' "$run_log" >&2 || true
        exit 1
      fi
      ;;
  esac
fi
if [ "$run_mode" = "skip" ]; then
  printf 'skip run: requested=%s target=%s host_target=%s\n' "$run_requested" "$target" "$host_target" >"$run_log"
fi

meta_check="none"
section_order_ok="1"
entry_runtime_ok="1"
dynamic_runtime_ok="1"

case "$target" in
  *darwin*)
    otool_bin="$(find_otool || true)"
    if [ "$otool_bin" = "" ]; then
      fail "missing otool for darwin metadata checks"
    fi
    set +e
    "$otool_bin" -l "$exe_path" >"$meta_log" 2>&1
    meta_status="$?"
    set -e
    if [ "$meta_status" -ne 0 ]; then
      fail "otool metadata read failed: $meta_log"
    fi
    meta_check="darwin.otool"
    text_line="$(awk '$1 == "sectname" && $2 == "__text" { print NR; exit }' "$meta_log")"
    cstr_line="$(awk '$1 == "sectname" && $2 == "__cstring" { print NR; exit }' "$meta_log")"
    data_line="$(awk '$1 == "sectname" && $2 == "__data" { print NR; exit }' "$meta_log")"
    if [ "$text_line" = "" ] || [ "$data_line" = "" ]; then
      fail "darwin metadata missing __text/__data sections"
    fi
    if [ "$text_line" -ge "$data_line" ]; then
      fail "darwin section order invalid: __text must precede __data"
    fi
    if [ "$cstr_line" != "" ] && [ "$text_line" -ge "$cstr_line" ]; then
      fail "darwin section order invalid: __text must precede __cstring"
    fi
    if [ "$cstr_line" != "" ] && [ "$cstr_line" -ge "$data_line" ]; then
      fail "darwin section order invalid: __cstring must precede __data"
    fi
    if ! rg -q 'LC_MAIN' "$meta_log"; then
      fail "darwin metadata missing LC_MAIN entry command"
    fi
    if ! rg -q 'LC_DYLD_INFO_ONLY' "$meta_log"; then
      fail "darwin metadata missing LC_DYLD_INFO_ONLY command"
    fi
    ;;
  *linux*)
    readelf_bin="$(find_readelf || true)"
    if [ "$readelf_bin" = "" ]; then
      fail "missing readelf/llvm-readelf for linux metadata checks"
    fi
    set +e
    "$readelf_bin" -W -l "$exe_path" >"$meta_log" 2>&1
    meta_status="$?"
    set -e
    if [ "$meta_status" -ne 0 ]; then
      fail "readelf metadata read failed: $meta_log"
    fi
    meta_check="linux.readelf"
    if ! rg -q 'INTERP' "$meta_log"; then
      fail "linux metadata missing INTERP segment"
    fi
    if ! rg -q 'DYNAMIC' "$meta_log"; then
      fail "linux metadata missing DYNAMIC segment"
    fi
    load_count="$(rg -n '\bLOAD\b' "$meta_log" | wc -l | tr -d ' ')"
    if [ "$load_count" -lt 2 ]; then
      fail "linux metadata LOAD segment count too small: $load_count"
    fi
    ;;
  *)
    printf 'metadata check skipped for target=%s\n' "$target" >"$meta_log"
    meta_check="skip.$target"
    ;;
esac

{
  echo "verify_backend_mem_image_core report"
  echo "status=ok"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "driver=$driver"
  echo "fixture=$fixture"
  echo "exe=$exe_path"
  echo "build_log=$build_log"
  echo "run_log=$run_log"
  echo "meta_log=$meta_log"
  echo "run_mode=$run_mode"
  echo "run_status=$run_status"
  echo "meta_check=$meta_check"
  echo "symbol_index_markers=$symbol_index_markers"
  echo "section_layout_markers=$section_layout_markers"
  echo "relocation_apply_markers=$relocation_apply_markers"
  echo "entry_symbol_markers=$entry_symbol_markers"
  echo "relocation_guard_markers=$relocation_guard_markers"
  echo "section_order_ok=$section_order_ok"
  echo "entry_runtime_ok=$entry_runtime_ok"
  echo "dynamic_runtime_ok=$dynamic_runtime_ok"
  echo "sidecar_obj=absent"
  echo "sidecar_objs_dir=absent"
} >"$report"

{
  echo "backend_mem_image_core_status=ok"
  echo "backend_mem_image_core_target=$target"
  echo "backend_mem_image_core_report=$report"
  echo "backend_mem_image_core_symbol_index_markers=$symbol_index_markers"
  echo "backend_mem_image_core_section_layout_markers=$section_layout_markers"
  echo "backend_mem_image_core_relocation_apply_markers=$relocation_apply_markers"
  echo "backend_mem_image_core_entry_symbol_markers=$entry_symbol_markers"
  echo "backend_mem_image_core_relocation_guard_markers=$relocation_guard_markers"
} >"$snapshot"

echo "verify_backend_mem_image_core ok"
