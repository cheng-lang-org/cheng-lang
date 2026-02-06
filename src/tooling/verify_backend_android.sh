#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_backend_mvp_driver_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"
linker_mode="${CHENG_BACKEND_LINKER:-self}"
target="aarch64-linux-android"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="${CHENG_BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"


detect_ndk_from_sdk() {
  sdk="$1"
  if [ -z "$sdk" ]; then
    return 1
  fi
  if [ -d "$sdk/ndk-bundle" ]; then
    echo "$sdk/ndk-bundle"
    return 0
  fi
  if [ -d "$sdk/ndk" ]; then
    latest="$(find "$sdk/ndk" -maxdepth 1 -mindepth 1 -type d -print 2>/dev/null | LC_ALL=C sort | tail -n 1 || true)"
    if [ -n "$latest" ]; then
      echo "$latest"
      return 0
    fi
  fi
  return 1
}

ndk="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
if [ -z "$ndk" ]; then
  sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
  if [ -z "$sdk" ]; then
    if [ -d "$HOME/Library/Android/sdk" ]; then
      sdk="$HOME/Library/Android/sdk"
    elif [ -d "$HOME/Android/Sdk" ]; then
      sdk="$HOME/Android/Sdk"
    fi
  fi
  ndk="$(detect_ndk_from_sdk "$sdk" || true)"
fi
if [ -z "$ndk" ] || [ ! -d "$ndk" ]; then
  echo "[verify_backend_android] missing NDK (install via Android Studio -> SDK Manager -> SDK Tools -> NDK (Side by side))" >&2
  echo "[verify_backend_android] set ANDROID_NDK_HOME or ANDROID_SDK_ROOT to run" >&2
  exit 2
fi

host_prebuilt=""
for candidate in darwin-arm64 darwin-x86_64 linux-x86_64 linux-arm64 linux-aarch64 windows-x86_64 windows-x64; do
  if [ -d "$ndk/toolchains/llvm/prebuilt/$candidate" ]; then
    host_prebuilt="$candidate"
    break
  fi
done
if [ -z "$host_prebuilt" ]; then
  echo "[verify_backend_android] missing NDK host prebuilt in $ndk" >&2
  exit 2
fi

tool_bin="$ndk/toolchains/llvm/prebuilt/$host_prebuilt/bin"
api="${CHENG_ANDROID_API:-21}"
ndk_cc="$tool_bin/aarch64-linux-android${api}-clang"
ndk_readelf="$tool_bin/llvm-readelf"
ndk_nm="$tool_bin/llvm-nm"

if [ ! -x "$ndk_cc" ]; then
  echo "[verify_backend_android] missing tool: $ndk_cc" >&2
  exit 2
fi
if [ ! -x "$ndk_readelf" ]; then
  echo "[verify_backend_android] missing tool: $ndk_readelf" >&2
  exit 2
fi
if [ ! -x "$ndk_nm" ]; then
  echo "[verify_backend_android] missing tool: $ndk_nm" >&2
  exit 2
fi

out_dir="artifacts/backend_android"
mkdir -p "$out_dir"

if [ "$linker_mode" = "self" ]; then
  mkdir -p chengcache
  if [ ! -f "$runtime_obj" ] || [ "src/std/system_helpers_backend.cheng" -nt "$runtime_obj" ]; then
    CHENG_BACKEND_ALLOW_NO_MAIN=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=mvp \
    CHENG_BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
    CHENG_BACKEND_OUTPUT="$runtime_obj" \
    "$driver" >/dev/null
  fi
fi

for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/return_object_copy_assign.cheng \
               tests/cheng/backend/fixtures/return_global_assign.cheng \
               tests/cheng/backend/fixtures/return_store_deref.cheng \
               tests/cheng/backend/fixtures/return_if.cheng \
               tests/cheng/backend/fixtures/return_while_sum.cheng \
               tests/cheng/backend/fixtures/return_for_sum.cheng \
               tests/cheng/backend/fixtures/return_call9.cheng \
               tests/cheng/backend/fixtures/hello_puts.cheng \
               tests/cheng/backend/fixtures/hello_importc_puts.cheng \
               tests/cheng/backend/fixtures/hello_importc_free.cheng \
               tests/cheng/backend/fixtures/return_pkg_import_call.cheng
do
  echo "[verify_backend_android] build: $fixture" >&2
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/${base}.android"
  pkg_roots=""
  multi=""
  if [ "$fixture" = "tests/cheng/backend/fixtures/return_pkg_import_call.cheng" ]; then
    pkg_roots="tests/cheng/backend/pkgs"
    multi="1"
  fi
  if [ "$linker_mode" = "self" ]; then
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_MULTI="$multi" \
    CHENG_PKG_ROOTS="$pkg_roots" \
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver"
  else
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_LINKER=system \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_CC="$ndk_cc" \
    CHENG_BACKEND_CFLAGS='-fPIE' \
    CHENG_BACKEND_LDFLAGS='-fPIE -pie' \
    CHENG_BACKEND_MULTI="$multi" \
    CHENG_PKG_ROOTS="$pkg_roots" \
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver"
  fi

  if [ ! -s "$exe_path" ]; then
    echo "[verify_backend_android] missing output: $exe_path" >&2
    exit 1
  fi

  if ! "$ndk_readelf" -h "$exe_path" > "$out_dir/${base}.readelf.txt" 2>&1; then
    echo "[verify_backend_android] readelf failed: $exe_path" >&2
    cat "$out_dir/${base}.readelf.txt" >&2 || true
    exit 1
  fi
  grep -q "Machine:[[:space:]]*AArch64" "$out_dir/${base}.readelf.txt"

  if ! "$ndk_nm" -g "$exe_path" > "$out_dir/${base}.nm.txt" 2>&1; then
    echo "[verify_backend_android] nm failed: $exe_path" >&2
    cat "$out_dir/${base}.nm.txt" >&2 || true
    exit 1
  fi
  if grep -qi "no symbols" "$out_dir/${base}.nm.txt"; then
    :
  else
    grep -Eq "(_)?main$" "$out_dir/${base}.nm.txt"
  fi
done

echo "verify_backend_android ok"
