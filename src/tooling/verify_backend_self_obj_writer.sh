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
  echo "[verify_backend_self_obj_writer] missing NDK (set ANDROID_NDK_HOME or ANDROID_SDK_ROOT)" >&2
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
  echo "[verify_backend_self_obj_writer] missing NDK host prebuilt in $ndk" >&2
  exit 2
fi

tool_bin="$ndk/toolchains/llvm/prebuilt/$host_prebuilt/bin"
api="${ANDROID_API:-21}"
ndk_cc="$tool_bin/aarch64-linux-android${api}-clang"
ndk_readelf="$tool_bin/llvm-readelf"
ndk_nm="$tool_bin/llvm-nm"

if [ ! -x "$ndk_cc" ] || [ ! -x "$ndk_readelf" ] || [ ! -x "$ndk_nm" ]; then
  echo "[verify_backend_self_obj_writer] missing NDK tools under $tool_bin" >&2
  exit 2
fi

out_dir="artifacts/backend_self_obj"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_path="$out_dir/hello_importc_puts.self.o"

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=elf \
BACKEND_TARGET=aarch64-linux-android \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_path" \
"$driver"

if [ ! -s "$obj_path" ]; then
  echo "[verify_backend_self_obj_writer] missing output: $obj_path" >&2
  exit 1
fi

"$ndk_readelf" -h "$obj_path" > "$out_dir/hello_importc_puts.self.readelf.h.txt"
grep -q "Type:[[:space:]]*REL" "$out_dir/hello_importc_puts.self.readelf.h.txt"
grep -q "Machine:[[:space:]]*AArch64" "$out_dir/hello_importc_puts.self.readelf.h.txt"

"$ndk_readelf" -S "$obj_path" > "$out_dir/hello_importc_puts.self.readelf.s.txt"
grep -q "\\.text" "$out_dir/hello_importc_puts.self.readelf.s.txt"
grep -q "\\.rodata" "$out_dir/hello_importc_puts.self.readelf.s.txt"
grep -q "\\.rela\\.text" "$out_dir/hello_importc_puts.self.readelf.s.txt"

"$ndk_readelf" -r "$obj_path" > "$out_dir/hello_importc_puts.self.readelf.r.txt"
grep -q "R_AARCH64_ADR_PREL_PG_HI21" "$out_dir/hello_importc_puts.self.readelf.r.txt"
grep -q "R_AARCH64_ADD_ABS_LO12_NC" "$out_dir/hello_importc_puts.self.readelf.r.txt"
grep -q "R_AARCH64_CALL26" "$out_dir/hello_importc_puts.self.readelf.r.txt"

"$ndk_nm" -g "$obj_path" > "$out_dir/hello_importc_puts.self.nm.txt"
grep -q " T main$" "$out_dir/hello_importc_puts.self.nm.txt"
grep -q " U puts$" "$out_dir/hello_importc_puts.self.nm.txt"

exe_path="$out_dir/hello_importc_puts.self.android"
"$ndk_cc" "$obj_path" -fPIE -pie -o "$exe_path"

"$ndk_readelf" -h "$exe_path" > "$out_dir/hello_importc_puts.self.android.readelf.h.txt"
grep -q "Machine:[[:space:]]*AArch64" "$out_dir/hello_importc_puts.self.android.readelf.h.txt"

echo "verify_backend_self_obj_writer ok"
