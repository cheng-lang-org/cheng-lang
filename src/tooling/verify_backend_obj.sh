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

host_os="$(uname -s 2>/dev/null || echo unknown)"

target=""
nm_main="main"
nm_puts="puts"

case "$host_os" in
  Darwin)
    target="arm64-apple-darwin"
    nm_main="_main"
    nm_puts="_puts"
    ;;
  Linux)
    target="aarch64-unknown-linux-gnu"
    ;;
  *)
    echo "verify_backend_obj skip: unsupported host os: $host_os" 1>&2
    exit 2
    ;;
esac


out_dir="artifacts/backend_obj"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_path="$out_dir/hello_importc_puts.o"

BACKEND_EMIT=obj \
BACKEND_TARGET="$target" \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_path" \
"$driver"

if ! command -v nm >/dev/null 2>&1; then
  echo "verify_backend_obj skip: missing nm" 1>&2
  exit 2
fi

set +e
nm "$obj_path" > "$out_dir/hello_importc_puts.nm.txt" 2>&1
status_nm="$?"
set -e
if [ "$status_nm" -ne 0 ]; then
  echo "verify_backend_obj skip: nm cannot read object ($host_os): $obj_path" 1>&2
  cat "$out_dir/hello_importc_puts.nm.txt" 1>&2 || true
  exit 2
fi

grep -q " T $nm_main" "$out_dir/hello_importc_puts.nm.txt"
grep -q " U $nm_puts" "$out_dir/hello_importc_puts.nm.txt"

echo "verify_backend_obj ok"
