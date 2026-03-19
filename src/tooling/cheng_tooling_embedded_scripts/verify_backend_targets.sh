#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"


out_dir="artifacts/backend_targets"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"

compile_target() {
  target="$1"
  output="$2"
  log="$3"
  set +e
  BACKEND_LINKER=system \
  BACKEND_NO_RUNTIME_C=0 \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$output" \
  "$driver" >"$log" 2>&1
  rc="$?"
  set -e
  return "$rc"
}

is_darwin_only_bootstrap_reject() {
  log="$1"
  if [ ! -f "$log" ]; then
    return 1
  fi
  rg -q "uir_codegen: bootstrap path only supports darwin target|host-only expects aarch64/arm64" "$log"
}

magic_hex() {
  # Prints first 4 bytes as lowercase hex.
  od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n'
}

darwin_obj="$out_dir/hello_importc_puts.darwin.bin"
darwin_x64_obj="$out_dir/hello_importc_puts.darwin_x86_64.bin"
android_obj="$out_dir/hello_importc_puts.android.bin"
darwin_log="$out_dir/hello_importc_puts.darwin.log"
darwin_x64_log="$out_dir/hello_importc_puts.darwin_x86_64.log"
android_log="$out_dir/hello_importc_puts.android.log"

if ! compile_target "arm64-apple-darwin" "$darwin_obj" "$darwin_log"; then
  echo "[verify_backend_targets] failed to build darwin executable: $darwin_log" 1>&2
  sed -n '1,120p' "$darwin_log" 1>&2 || true
  exit 1
fi

[ "$(magic_hex "$darwin_obj")" = "cffaedfe" ]
if ! command -v nm >/dev/null 2>&1; then
  echo "verify_backend_targets skip: missing nm" 1>&2
  exit 2
fi
nm "$darwin_obj" | grep -q " T _main"
nm "$darwin_obj" | grep -q " U _puts"

darwin_x64_supported="1"
if ! compile_target "x86_64-apple-darwin" "$darwin_x64_obj" "$darwin_x64_log"; then
  if is_darwin_only_bootstrap_reject "$darwin_x64_log"; then
    darwin_x64_supported="0"
    echo "[verify_backend_targets] skip darwin x86_64 target: host-only path" 1>&2
  else
    echo "[verify_backend_targets] failed to build darwin x86_64 executable: $darwin_x64_log" 1>&2
    sed -n '1,120p' "$darwin_x64_log" 1>&2 || true
    exit 1
  fi
fi

if [ "$darwin_x64_supported" = "1" ]; then
  [ "$(magic_hex "$darwin_x64_obj")" = "cffaedfe" ]
  nm "$darwin_x64_obj" | grep -q " T _main"
fi

android_supported="1"
if ! compile_target "aarch64-linux-android" "$android_obj" "$android_log"; then
  android_supported="0"
  if is_darwin_only_bootstrap_reject "$android_log"; then
    echo "[verify_backend_targets] skip android target: bootstrap darwin-only path" 1>&2
  else
    echo "[verify_backend_targets] skip android target: system-link cross-link unavailable on this host" 1>&2
  fi
fi

if [ "$android_supported" = "1" ]; then
  [ "$(magic_hex "$android_obj")" = "7f454c46" ]
  if command -v llvm-nm >/dev/null 2>&1; then
    llvm-nm "$android_obj" | grep -q " T main"
  elif nm "$android_obj" >/dev/null 2>&1; then
    nm "$android_obj" | grep -q " T main"
  fi
fi

echo "verify_backend_targets ok"
