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

if [ "$(uname -s 2>/dev/null || true)" != "Darwin" ]; then
  echo "[verify_backend_self_obj_writer_macho] non-Darwin host" >&2
  exit 2
fi

if ! command -v otool >/dev/null 2>&1; then
  echo "[verify_backend_self_obj_writer_macho] missing otool" >&2
  exit 2
fi
if ! command -v nm >/dev/null 2>&1; then
  echo "[verify_backend_self_obj_writer_macho] missing nm" >&2
  exit 2
fi
if ! command -v cc >/dev/null 2>&1; then
  echo "[verify_backend_self_obj_writer_macho] missing cc" >&2
  exit 2
fi


out_dir="artifacts/backend_self_obj_macho"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_path="$out_dir/hello_importc_puts.self.macho.o"

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=macho \
BACKEND_TARGET=arm64-apple-darwin \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_path" \
"$driver"

if [ ! -s "$obj_path" ]; then
  echo "[verify_backend_self_obj_writer_macho] missing output: $obj_path" >&2
  exit 1
fi

otool -l "$obj_path" > "$out_dir/hello_importc_puts.self.macho.otool.l.txt"
grep -q "LC_SEGMENT_64" "$out_dir/hello_importc_puts.self.macho.otool.l.txt"
grep -q "__text" "$out_dir/hello_importc_puts.self.macho.otool.l.txt"
grep -q "__cstring" "$out_dir/hello_importc_puts.self.macho.otool.l.txt"
grep -q "LC_SYMTAB" "$out_dir/hello_importc_puts.self.macho.otool.l.txt"

otool -rv "$obj_path" > "$out_dir/hello_importc_puts.self.macho.otool.rv.txt"
grep -q "BR26" "$out_dir/hello_importc_puts.self.macho.otool.rv.txt"
grep -q "PAGE21" "$out_dir/hello_importc_puts.self.macho.otool.rv.txt"
grep -q "PAGOF12" "$out_dir/hello_importc_puts.self.macho.otool.rv.txt"

nm -nm "$obj_path" > "$out_dir/hello_importc_puts.self.macho.nm.txt"
grep -q " external _main$" "$out_dir/hello_importc_puts.self.macho.nm.txt"
grep -q " external _puts$" "$out_dir/hello_importc_puts.self.macho.nm.txt"

if [ "$(uname -m 2>/dev/null || true)" = "arm64" ]; then
  exe_path="$out_dir/hello_importc_puts.self.macho.exe"
  cc "$obj_path" -o "$exe_path"
  "$exe_path" > "$out_dir/hello_importc_puts.self.macho.run.txt"
  grep -q "hello importc puts" "$out_dir/hello_importc_puts.self.macho.run.txt"
fi

echo "verify_backend_self_obj_writer_macho ok"
