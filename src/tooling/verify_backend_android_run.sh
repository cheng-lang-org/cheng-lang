#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if ! command -v adb >/dev/null 2>&1; then
  echo "[verify_backend_android_run] missing tool: adb" >&2
  exit 2
fi

serial="${ANDROID_SERIAL:-}"
if [ -z "$serial" ]; then
  serial="$(adb devices | awk 'NR>1 && $2 == "device" {print $1; exit}')"
fi
if [ -z "$serial" ]; then
  echo "[verify_backend_android_run] no Android device/emulator detected (adb devices)" >&2
  echo "[verify_backend_android_run] set ANDROID_SERIAL to select a device" >&2
  exit 2
fi

if [ "${ANDROID_REBUILD:-0}" = "1" ] || ! ls artifacts/backend_android/*.android >/dev/null 2>&1; then
  sh src/tooling/verify_backend_android.sh
fi

remote_dir="/data/local/tmp/cheng_backend"
adb -s "$serial" shell "mkdir -p '$remote_dir'"

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
  base="$(basename "$fixture" .cheng)"
  exe_path="artifacts/backend_android/${base}.android"
  if [ ! -s "$exe_path" ]; then
    echo "[verify_backend_android_run] missing output: $exe_path" >&2
    exit 1
  fi
  remote_path="$remote_dir/${base}.android"
  echo "[verify_backend_android_run] push: ${base}.android" >&2
  adb -s "$serial" push "$exe_path" "$remote_path" >/dev/null 2>&1
  adb -s "$serial" shell "chmod 755 '$remote_path'"

  echo "[verify_backend_android_run] run: ${base}.android" >&2
  adb -s "$serial" shell "$remote_path"
done

echo "verify_backend_android_run ok"
