#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    rc=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$rc"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

fail() {
  echo "[verify_backend_mem_exe_emit] $1" >&2
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
    "${BACKEND_MEM_EXE_EMIT_DRIVER:-}" \
    "${BACKEND_DRIVER:-}" \
    "$path_from_resolver" \
    "src/tooling/backend_driver_exec.sh" \
    "artifacts/backend_driver/cheng" \
    "artifacts/backend_driver/cheng.fixed3" \
    "dist/releases/current/cheng" \
    "artifacts/backend_seed/cheng.stage2"; do
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
target="${BACKEND_TARGET:-${BACKEND_MEM_EXE_EMIT_TARGET:-$host_target}}"
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

out_dir="artifacts/backend_mem_exe_emit"
rm -rf "$out_dir"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"

linker_shared_file="src/backend/obj/linker_shared_core.cheng"
macho_file="src/backend/obj/macho_linker.cheng"
macho_x64_file="src/backend/obj/macho_linker_x86_64.cheng"
elf_file="src/backend/obj/elf_linker.cheng"
elf_rv64_file="src/backend/obj/elf_linker_riscv64.cheng"
coff_file="src/backend/obj/coff_linker.cheng"
driver_file="src/backend/tooling/backend_driver.cheng"
chengc_file="src/tooling/chengc.sh"
link_env_file="src/tooling/backend_link_env.sh"

format_emit_markers=0
runtime_merge_markers=0
output_tx_markers=0
sidecar_zero_marker=0

shared_atomic_fn_marker="$(count_marker "$linker_shared_file" 'fn linkerCoreWriteFileAtomic\(')"
shared_atomic_tmp_marker="$(count_marker "$linker_shared_file" 'let tmpPath: str = linkerCoreBuildTmpOutputPath\(outputPath\)')"
shared_atomic_rename_marker="$(count_marker "$linker_shared_file" 'os\.renameFile\(tmpPath, outputPath\)')"
output_tx_markers=$((shared_atomic_fn_marker + shared_atomic_tmp_marker + shared_atomic_rename_marker))
if [ "$output_tx_markers" -lt 3 ]; then
  fail "missing ATOM-08 output-transaction markers in linker_shared_core"
fi

macho_emit_marker="$(count_marker "$macho_file" 'linkerCoreWriteFileAtomic\(outputPath')"
macho_x64_emit_marker="$(count_marker "$macho_x64_file" 'linkerCoreWriteFileAtomic\(outputPath')"
elf_emit_marker="$(count_marker "$elf_file" 'linkerCoreWriteFileAtomic\(outputPath')"
elf_rv64_emit_marker="$(count_marker "$elf_rv64_file" 'linkerCoreWriteFileAtomic\(outputPath')"
coff_emit_marker="$(count_marker "$coff_file" 'linkerCoreWriteFileAtomic\(outputPath')"
format_emit_markers=$((macho_emit_marker + macho_x64_emit_marker + elf_emit_marker + elf_rv64_emit_marker + coff_emit_marker))
if [ "$format_emit_markers" -lt 5 ]; then
  fail "missing ATOM-06 format-emit markers in linker files"
fi

runtime_removed_diag_marker="$(count_marker "$driver_file" 'self linker requires BACKEND_RUNTIME_OBJ')"
if [ "$runtime_removed_diag_marker" -ne 0 ]; then
  fail "found removed runtime-obj contract diagnostic in backend_driver"
fi
runtime_obj_chengc_marker="$(count_marker "$chengc_file" 'BACKEND_RUNTIME_OBJ=')"
runtime_obj_link_env_marker="$(count_marker "$link_env_file" 'BACKEND_RUNTIME_OBJ=')"
runtime_merge_markers=$((runtime_obj_chengc_marker + runtime_obj_link_env_marker))
if [ "$runtime_merge_markers" -lt 2 ]; then
  fail "missing runtime-object wiring markers in tooling entrypoints"
fi

sidecar_zero_marker="$(count_marker "docs/cheng-plan-full.md" 'sidecar 残留为 0')"
if [ "$sidecar_zero_marker" -lt 1 ]; then
  fail "missing sidecar zero requirement marker in docs/cheng-plan-full.md"
fi

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
chengc_fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
if [ ! -f "$fixture" ]; then
  fail "missing fixture: $fixture"
fi
if [ ! -f "$chengc_fixture" ]; then
  fail "missing fixture: $chengc_fixture"
fi

runtime_free_exe="$out_dir/hello_puts.$safe_target.runtime_off"
runtime_free_log="$out_dir/hello_puts.$safe_target.runtime_off.log"
runtime_env_log="$out_dir/hello_puts.$safe_target.runtime_env.log"
chengc_exe="$out_dir/hello_importc_puts.$safe_target"
chengc_build_log="$out_dir/hello_importc_puts.$safe_target.build.log"
meta_log="$out_dir/hello_importc_puts.$safe_target.meta.log"
report="$out_dir/backend_mem_exe_emit.$safe_target.report.txt"
snapshot="$out_dir/backend_mem_exe_emit.$safe_target.snapshot.env"
require_driver_sidecar_zero="${BACKEND_MEM_EXE_EMIT_REQUIRE_DRIVER_SIDECAR_ZERO:-1}"

case "$target" in
  *windows*|*msvc*)
    runtime_free_exe="$runtime_free_exe.exe"
    chengc_exe="$chengc_exe.exe"
    ;;
esac

rm -f \
  "$runtime_free_exe" "$runtime_free_log" "$runtime_env_log" \
  "$chengc_exe" "$chengc_build_log" "$meta_log" \
  "$report" "$snapshot"
rm -rf \
  "$runtime_free_exe.objs" "$runtime_free_exe.objs.lock" \
  "$chengc_exe.objs" "$chengc_exe.objs.lock"
rm -f \
  "$runtime_free_exe.o" "$chengc_exe.o" \
  "$runtime_free_exe.tmp" "$chengc_exe.tmp"

set +e
runtime_env_line="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:self 2>"$runtime_env_log")"
runtime_env_status="$?"
set -e
if [ "$runtime_env_status" -ne 0 ]; then
  fail "backend_link_env failed for target=$target (log: $runtime_env_log)"
fi
runtime_obj=""
for token in $runtime_env_line; do
  case "$token" in
    BACKEND_RUNTIME_OBJ=*)
      runtime_obj="${token#BACKEND_RUNTIME_OBJ=}"
      ;;
  esac
done
if [ "$runtime_obj" = "" ] || [ ! -s "$runtime_obj" ]; then
  fail "resolved runtime object missing: $runtime_obj"
fi

set +e
# shellcheck disable=SC2086
env $runtime_env_line \
  MM="${MM:-orc}" \
  ABI=v2_noptr \
  BACKEND_DRIVER="$driver" \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_EMIT=exe \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$runtime_free_exe" \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_INCREMENTAL=0 \
  STAGE1_STD_NO_POINTERS=0 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
  STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
  "$driver" >"$runtime_free_log" 2>&1
runtime_free_status="$?"
set -e
if [ "$runtime_free_status" -ne 0 ]; then
  echo "[verify_backend_mem_exe_emit] runtime-linked build failed (status=$runtime_free_status): $runtime_free_log" >&2
  sed -n '1,200p' "$runtime_free_log" >&2 || true
  exit 1
fi
if [ ! -s "$runtime_free_exe" ]; then
  fail "missing runtime-off executable: $runtime_free_exe"
fi
runtime_free_sidecar_obj="absent"
runtime_free_sidecar_objs_dir="absent"
runtime_free_sidecar_tmp="absent"
if [ -e "$runtime_free_exe.o" ]; then
  runtime_free_sidecar_obj="present"
fi
if [ -d "$runtime_free_exe.objs" ] || [ -d "$runtime_free_exe.objs.lock" ]; then
  runtime_free_sidecar_objs_dir="present"
fi
if [ -e "$runtime_free_exe.tmp" ]; then
  runtime_free_sidecar_tmp="present"
fi
if [ "$runtime_free_sidecar_obj" = "present" ] || [ "$runtime_free_sidecar_objs_dir" = "present" ] || [ "$runtime_free_sidecar_tmp" = "present" ]; then
  if [ "$require_driver_sidecar_zero" = "1" ]; then
    fail "runtime-off executable has sidecar residue"
  fi
  rm -f "$runtime_free_exe.o" "$runtime_free_exe.tmp"
  rm -rf "$runtime_free_exe.objs" "$runtime_free_exe.objs.lock"
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
  STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
  STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
  sh src/tooling/chengc.sh "$chengc_fixture" --frontend:stage1 --emit:exe --out:"$chengc_exe" >"$chengc_build_log" 2>&1
chengc_status="$?"
set -e
if [ "$chengc_status" -ne 0 ]; then
  echo "[verify_backend_mem_exe_emit] chengc emit=exe failed (status=$chengc_status): $chengc_build_log" >&2
  sed -n '1,200p' "$chengc_build_log" >&2 || true
  exit 1
fi
if [ ! -s "$chengc_exe" ]; then
  fail "missing chengc executable output: $chengc_exe"
fi
if [ -e "$chengc_exe.o" ] || [ -d "$chengc_exe.objs" ] || [ -d "$chengc_exe.objs.lock" ] || [ -e "$chengc_exe.tmp" ]; then
  fail "chengc emit=exe path has sidecar residue"
fi

meta_check="none"
case "$target" in
  *darwin*)
    otool_bin="$(find_otool || true)"
    if [ "$otool_bin" = "" ]; then
      fail "missing otool for darwin metadata checks"
    fi
    set +e
    "$otool_bin" -l "$chengc_exe" >"$meta_log" 2>&1
    meta_status="$?"
    set -e
    if [ "$meta_status" -ne 0 ]; then
      fail "otool metadata read failed: $meta_log"
    fi
    if ! rg -q 'LC_MAIN' "$meta_log"; then
      fail "darwin metadata missing LC_MAIN entry command"
    fi
    if ! rg -q 'LC_DYLD_INFO_ONLY' "$meta_log"; then
      fail "darwin metadata missing LC_DYLD_INFO_ONLY command"
    fi
    meta_check="darwin.otool"
    ;;
  *linux*)
    readelf_bin="$(find_readelf || true)"
    if [ "$readelf_bin" = "" ]; then
      fail "missing readelf/llvm-readelf for linux metadata checks"
    fi
    set +e
    "$readelf_bin" -W -l "$chengc_exe" >"$meta_log" 2>&1
    meta_status="$?"
    set -e
    if [ "$meta_status" -ne 0 ]; then
      fail "readelf metadata read failed: $meta_log"
    fi
    if ! rg -q '\bLOAD\b' "$meta_log"; then
      fail "linux metadata missing LOAD segments"
    fi
    meta_check="linux.readelf"
    ;;
  *)
    printf 'metadata check skipped for target=%s\n' "$target" >"$meta_log"
    meta_check="skip.$target"
    ;;
esac

{
  echo "verify_backend_mem_exe_emit report"
  echo "status=ok"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "driver=$driver"
  echo "fixture=$fixture"
  echo "chengc_fixture=$chengc_fixture"
  echo "runtime_free_exe=$runtime_free_exe"
  echo "runtime_free_log=$runtime_free_log"
  echo "runtime_free_status=$runtime_free_status"
  echo "runtime_free_sidecar_obj=$runtime_free_sidecar_obj"
  echo "runtime_free_sidecar_objs_dir=$runtime_free_sidecar_objs_dir"
  echo "runtime_free_sidecar_tmp=$runtime_free_sidecar_tmp"
  echo "require_driver_sidecar_zero=$require_driver_sidecar_zero"
  echo "runtime_env_log=$runtime_env_log"
  echo "runtime_obj=$runtime_obj"
  echo "chengc_exe=$chengc_exe"
  echo "chengc_build_log=$chengc_build_log"
  echo "meta_log=$meta_log"
  echo "meta_check=$meta_check"
  echo "format_emit_markers=$format_emit_markers"
  echo "runtime_merge_markers=$runtime_merge_markers"
  echo "output_tx_markers=$output_tx_markers"
  echo "sidecar_zero_marker=$sidecar_zero_marker"
  echo "sidecar_obj=absent"
  echo "sidecar_objs_dir=absent"
  echo "tmp_residue=absent"
} >"$report"

{
  echo "backend_mem_exe_emit_status=ok"
  echo "backend_mem_exe_emit_target=$target"
  echo "backend_mem_exe_emit_report=$report"
  echo "backend_mem_exe_emit_format_emit_markers=$format_emit_markers"
  echo "backend_mem_exe_emit_runtime_merge_markers=$runtime_merge_markers"
  echo "backend_mem_exe_emit_output_tx_markers=$output_tx_markers"
  echo "backend_mem_exe_emit_runtime_obj=$runtime_obj"
} >"$snapshot"

echo "verify_backend_mem_exe_emit ok"
