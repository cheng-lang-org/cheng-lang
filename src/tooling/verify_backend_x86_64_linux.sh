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

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"
case "$host_os/$host_arch" in
  Linux/x86_64)
    ;;
  *)
    echo "verify_backend_x86_64_linux skip: linux/x86_64 only (host=$host_os/$host_arch)" >&2
    exit 2
    ;;
esac

if ! command -v cc >/dev/null 2>&1; then
  echo "verify_backend_x86_64_linux skip: missing cc" >&2
  exit 2
fi

driver="$(sh src/tooling/backend_driver_path.sh)"

out_dir="artifacts/backend_x86_64_linux"
mkdir -p "$out_dir"

magic_hex() {
  od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n'
}

build_obj() {
  fixture="$1"
  obj_path="$2"
  rm -f "$obj_path"
  BACKEND_VALIDATE=1 \
  BACKEND_EMIT=obj \
  BACKEND_TARGET=x86_64-unknown-linux-gnu \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$obj_path" \
  "$driver"
  if [ ! -s "$obj_path" ]; then
    echo "[Error] missing object output: $obj_path" >&2
    exit 1
  fi
}

build_exe() {
  fixture="$1"
  exe_path="$2"
  rm -f "$exe_path"
  BACKEND_VALIDATE=1 \
  BACKEND_EMIT=exe \
  BACKEND_TARGET=x86_64-unknown-linux-gnu \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$exe_path" \
  "$driver"
  if [ ! -x "$exe_path" ]; then
    echo "[Error] missing executable output: $exe_path" >&2
    exit 1
  fi
}

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_path="$out_dir/hello_importc_puts.x86_64.o"
exe_path="$out_dir/hello_importc_puts.x86_64"

build_obj "$fixture" "$obj_path"
[ "$(magic_hex "$obj_path")" = "7f454c46" ]
if command -v nm >/dev/null 2>&1; then
  nm "$obj_path" | grep -q " T main"
  nm "$obj_path" | grep -q " U puts"
fi

build_exe "$fixture" "$exe_path"
"$exe_path" >"$out_dir/run.hello_importc_puts.txt"
grep -Fq "hello importc puts" "$out_dir/run.hello_importc_puts.txt"

for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/return_call9.cheng \
               tests/cheng/backend/fixtures/return_while_sum.cheng \
               tests/cheng/backend/fixtures/return_for_sum.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/${base}.x86_64"
  build_exe "$fixture" "$exe_path"
  "$exe_path"
done

echo "verify_backend_x86_64_linux ok"
