#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"


out_dir="artifacts/backend_targets"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"

magic_hex() {
  # Prints first 4 bytes as lowercase hex.
  od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n'
}

darwin_obj="$out_dir/hello_importc_puts.darwin.o"
darwin_x64_obj="$out_dir/hello_importc_puts.darwin_x86_64.o"
android_obj="$out_dir/hello_importc_puts.android.o"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=arm64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$darwin_obj" \
"$driver"

[ "$(magic_hex "$darwin_obj")" = "cffaedfe" ]
if ! command -v nm >/dev/null 2>&1; then
  echo "verify_backend_targets skip: missing nm" 1>&2
  exit 2
fi
nm "$darwin_obj" | grep -q " T _main"
nm "$darwin_obj" | grep -q " U _puts"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=x86_64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$darwin_x64_obj" \
"$driver"

[ "$(magic_hex "$darwin_x64_obj")" = "cffaedfe" ]
nm "$darwin_x64_obj" | grep -q " T _main"
nm "$darwin_x64_obj" | grep -q " U _puts"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=aarch64-linux-android \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$android_obj" \
"$driver"

[ "$(magic_hex "$android_obj")" = "7f454c46" ]
if command -v llvm-nm >/dev/null 2>&1; then
  llvm-nm "$android_obj" | grep -q " T main"
  llvm-nm "$android_obj" | grep -q " U puts"
elif nm "$android_obj" >/dev/null 2>&1; then
  nm "$android_obj" | grep -q " T main"
  nm "$android_obj" | grep -q " U puts"
fi

echo "verify_backend_targets ok"
