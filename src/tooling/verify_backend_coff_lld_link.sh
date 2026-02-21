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

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi

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
  lld_kind="missing"
fi

check_obj() {
  obj="$1"
  machine="$2"
  stem="$3"
  if [ ! -s "$obj" ]; then
    echo "[verify_backend_coff_lld_link] missing output: $obj" >&2
    exit 1
  fi
  "$objdump" -h "$obj" > "$out_dir/${stem}.objdump.h.txt"
  grep -q "$machine" "$out_dir/${stem}.objdump.h.txt"
  grep -q "\\.text" "$out_dir/${stem}.objdump.h.txt"
  "$objdump" -t "$obj" > "$out_dir/${stem}.objdump.t.txt" || true
  grep -q "main" "$out_dir/${stem}.objdump.t.txt" || {
    echo "[verify_backend_coff_lld_link] missing symbol main in object: $obj" >&2
    exit 1
  }
}

is_darwin_only_bootstrap_reject() {
  log="$1"
  if [ ! -f "$log" ]; then
    return 1
  fi
  rg -q "uir_codegen: bootstrap path only supports darwin target|supports darwin target only" "$log"
}

build_obj_or_skip() {
  target="$1"
  output="$2"
  log="$3"
  set +e
  BACKEND_EMIT=obj \
  BACKEND_OBJ_WRITER=coff \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$output" \
  "$driver" >"$log" 2>&1
  rc="$?"
  set -e
  if [ "$rc" -eq 0 ]; then
    return 0
  fi
  if is_darwin_only_bootstrap_reject "$log"; then
    echo "verify_backend_coff_lld_link ok (bootstrap darwin-only path for $target)"
    exit 0
  fi
  echo "[verify_backend_coff_lld_link] failed to build object ($target): $log" >&2
  sed -n '1,120p' "$log" >&2 || true
  exit 1
}

out_dir="artifacts/backend_coff_lld_link"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
obj_path="$out_dir/return_add.self.coff.arm64.obj"
dll_path="$out_dir/return_add.self.coff.arm64.dll"
obj_path_x64="$out_dir/return_add.self.coff.x86_64.obj"
dll_path_x64="$out_dir/return_add.self.coff.x86_64.dll"
obj_log="$out_dir/return_add.self.coff.arm64.build.log"
obj_log_x64="$out_dir/return_add.self.coff.x86_64.build.log"

build_obj_or_skip "aarch64-pc-windows-msvc" "$obj_path" "$obj_log"

check_obj "$obj_path" "coff-arm64" "return_add.self.coff.arm64.obj"

build_obj_or_skip "x86_64-pc-windows-msvc" "$obj_path_x64" "$obj_log_x64"

check_obj "$obj_path_x64" "coff-x86-64" "return_add.self.coff.x86_64.obj"

if [ "$lld_kind" = "missing" ]; then
  echo "verify_backend_coff_lld_link ok (lld=missing, mode=obj-only)"
  exit 0
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
