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

driver="$(sh src/tooling/backend_driver_path.sh)"

if ! command -v xcrun >/dev/null 2>&1; then
  echo "[verify_backend_self_obj_writer_coff] missing xcrun" >&2
  exit 2
fi
if ! xcrun --find llvm-objdump >/dev/null 2>&1; then
  echo "[verify_backend_self_obj_writer_coff] missing llvm-objdump (xcrun)" >&2
  exit 2
fi
if ! xcrun --find llvm-nm >/dev/null 2>&1; then
  echo "[verify_backend_self_obj_writer_coff] missing llvm-nm (xcrun)" >&2
  exit 2
fi


out_dir="artifacts/backend_self_obj_coff"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_path="$out_dir/hello_importc_puts.self.coff.obj"
obj_path_x64="$out_dir/hello_importc_puts.self.coff.x86_64.obj"

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=coff \
BACKEND_TARGET=aarch64-pc-windows-msvc \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_path" \
"$driver"

if [ ! -s "$obj_path" ]; then
  echo "[verify_backend_self_obj_writer_coff] missing output: $obj_path" >&2
  exit 1
fi

xcrun llvm-objdump -h -r "$obj_path" > "$out_dir/hello_importc_puts.self.coff.objdump.hr.txt"
grep -q "file format coff-arm64" "$out_dir/hello_importc_puts.self.coff.objdump.hr.txt"
grep -q "\\.text" "$out_dir/hello_importc_puts.self.coff.objdump.hr.txt"
grep -q "\\.rdata" "$out_dir/hello_importc_puts.self.coff.objdump.hr.txt"
grep -q "IMAGE_REL_ARM64_PAGEBASE_REL21" "$out_dir/hello_importc_puts.self.coff.objdump.hr.txt"
grep -q "IMAGE_REL_ARM64_PAGEOFFSET_12A" "$out_dir/hello_importc_puts.self.coff.objdump.hr.txt"
grep -q "IMAGE_REL_ARM64_BRANCH26" "$out_dir/hello_importc_puts.self.coff.objdump.hr.txt"

xcrun llvm-nm -n "$obj_path" > "$out_dir/hello_importc_puts.self.coff.nm.txt"
grep -q " T main$" "$out_dir/hello_importc_puts.self.coff.nm.txt"
grep -q " U puts$" "$out_dir/hello_importc_puts.self.coff.nm.txt"

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=coff \
BACKEND_TARGET=x86_64-pc-windows-msvc \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_path_x64" \
"$driver"

if [ ! -s "$obj_path_x64" ]; then
  echo "[verify_backend_self_obj_writer_coff] missing output: $obj_path_x64" >&2
  exit 1
fi

xcrun llvm-objdump -h -r "$obj_path_x64" > "$out_dir/hello_importc_puts.self.coff.x86_64.objdump.hr.txt"
grep -q "file format coff-x86-64" "$out_dir/hello_importc_puts.self.coff.x86_64.objdump.hr.txt"
grep -q "\\.text" "$out_dir/hello_importc_puts.self.coff.x86_64.objdump.hr.txt"
grep -q "\\.rdata" "$out_dir/hello_importc_puts.self.coff.x86_64.objdump.hr.txt"
grep -q "IMAGE_REL_AMD64_REL32" "$out_dir/hello_importc_puts.self.coff.x86_64.objdump.hr.txt"

xcrun llvm-nm -n "$obj_path_x64" > "$out_dir/hello_importc_puts.self.coff.x86_64.nm.txt"
grep -q " T main$" "$out_dir/hello_importc_puts.self.coff.x86_64.nm.txt"
grep -q " U puts$" "$out_dir/hello_importc_puts.self.coff.x86_64.nm.txt"

echo "verify_backend_self_obj_writer_coff ok"
