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
      printf '%s\n' "$p"
      return 0
    fi
  fi
  if command -v llvm-objdump >/dev/null 2>&1; then
    command -v llvm-objdump
    return 0
  fi
  if command -v objdump >/dev/null 2>&1; then
    command -v objdump
    return 0
  fi
  return 1
}

probe_fixture="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$probe_fixture" ]; then
  probe_fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi

driver_help_ok() {
  bin="$1"
  [ -x "$bin" ] || return 1
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
  out_base="${out%.*}"
  rm -f "$out" "$out.o" "$out.tmp" "$out.tmp.linkobj" "$out_base.o" "$out_base.tmp" "$out_base.tmp.linkobj"
  rm -rf "$out.objs" "$out.objs.lock" "$out_base.objs" "$out_base.objs.lock"
  set +e
  env \
    BACKEND_LINKER=self \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ= \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_fixture" \
    BACKEND_OUTPUT="$out" \
    "$bin" >/dev/null 2>&1
  status="$?"
  set -e
  rm -f "$out.o" "$out.tmp" "$out.tmp.linkobj" "$out_base.o" "$out_base.tmp" "$out_base.tmp.linkobj"
  rm -rf "$out.objs" "$out.objs.lock" "$out_base.objs" "$out_base.objs.lock"
  if [ "$status" -ne 0 ] || [ ! -s "$out" ]; then
    return 1
  fi
  return 0
}

pick_driver() {
  target="$1"
  resolver_path="$(BACKEND_DRIVER_PATH_USE_WRAPPER=0 BACKEND_DRIVER_PATH_ALLOW_SELFHOST=1 sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
  for cand in \
    "${BACKEND_SELF_LINKER_DRIVER:-}" \
    "${BACKEND_LINKER_ABI_CORE_DRIVER:-}" \
    "${BACKEND_DRIVER:-}" \
    "artifacts/backend_seed/cheng.stage2" \
    "artifacts/backend_selfhost_self_obj/cheng_stage0_default" \
    "dist/releases/current/cheng" \
    "$resolver_path"; do
    [ "$cand" != "" ] || continue
    [ -x "$cand" ] || continue
    if ! driver_help_ok "$cand"; then
      continue
    fi
    probe_out="artifacts/backend_self_linker_coff/.driver_probe.$(basename "$cand").$target"
    rm -f "$probe_out"
    if probe_driver_target "$cand" "$target" "$probe_out"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

objdump="$(find_llvm_objdump || true)"
if [ "$objdump" = "" ]; then
  echo "[verify_backend_self_linker_coff] missing objdump tool" >&2
  exit 1
fi

out_dir="artifacts/backend_self_linker_coff"
rm -rf "$out_dir"
mkdir -p "$out_dir"

target="${BACKEND_COFF_TARGET:-aarch64-pc-windows-msvc}"
driver="$(pick_driver "$target" || true)"
if [ "$driver" = "" ] || [ ! -x "$driver" ]; then
  echo "[verify_backend_self_linker_coff] no driver can self-link target: $target" >&2
  exit 1
fi

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
exe_path="$out_dir/hello_importc_puts.self.coff.exe"

run_build() {
  env \
    BACKEND_LINKER=self \
    BACKEND_COFF_CRT_DLL=UCRTBASE.dll \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ= \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver" >/dev/null
}

rm -f "$exe_path" "$exe_path.o" "$exe_path.tmp" "$exe_path.tmp.linkobj"
rm -rf "$exe_path.objs" "$exe_path.objs.lock"

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
exe_base="${exe_path%.*}"
rm -f "$exe_path.o" "$exe_path.tmp" "$exe_path.tmp.linkobj" "$exe_base.o" "$exe_base.tmp" "$exe_base.tmp.linkobj"
rm -rf "$exe_path.objs" "$exe_path.objs.lock" "$exe_base.objs" "$exe_base.objs.lock"

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
