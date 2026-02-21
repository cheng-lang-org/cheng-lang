#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/mobile_run_android.sh <file.cheng> [--name:<appName>] [--out:<dir>] [--assets:<dir>] [--plugins:<csv>] [--mm:<orc>] [--orc]

Builds the Android Studio template (NDK) and runs it on a connected device:
  - export .cheng -> Android project
  - ./gradlew assembleDebug
  - adb install -r
  - adb shell am start

Notes:
  - Requires Android SDK (ANDROID_SDK_ROOT/ANDROID_HOME or default macOS path).
  - If multiple devices are connected, set ANDROID_SERIAL.
EOF
}

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

in="$1"
shift || true

name=""
out=""
assets=""
plugins=""
mm=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --name:*)
      name="${1#--name:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --assets:*)
      assets="${1#--assets:}"
      ;;
    --plugins:*)
      plugins="${1#--plugins:}"
      ;;
    --orc)
      mm="orc"
      ;;
    --mm:*)
      mm="${1#--mm:}"
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

if [ "$mm" != "" ] && [ "$mm" != "orc" ]; then
  echo "[Error] invalid --mm:$mm (only orc is supported)" 1>&2
  exit 2
fi

if [ ! -f "$in" ]; then
  echo "[Error] file not found: $in" 1>&2
  exit 2
fi

if ! command -v adb >/dev/null 2>&1; then
  echo "[Error] missing tool: adb" 1>&2
  exit 2
fi

serial="${ANDROID_SERIAL:-}"
if [ -z "$serial" ]; then
  serial="$(adb devices | awk 'NR>1 && $2 == "device" {print $1; exit}')"
fi
if [ -z "$serial" ]; then
  echo "[Error] no Android device/emulator detected (adb devices)" 1>&2
  echo "[Error] set ANDROID_SERIAL to select a device" 1>&2
  exit 2
fi

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "$name" = "" ]; then
  base="$(basename "$in")"
  name="${base%.cheng}"
fi
if [ "$out" = "" ]; then
  out="mobile_build/$name"
fi
case "$out" in
  /*) ;;
  *) out="$root/$out" ;;
esac

sdk="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
if [ -z "$sdk" ]; then
  if [ -d "$HOME/Library/Android/sdk" ]; then
    sdk="$HOME/Library/Android/sdk"
  elif [ -d "$HOME/Android/Sdk" ]; then
    sdk="$HOME/Android/Sdk"
  fi
fi
if [ -z "$sdk" ] || [ ! -d "$sdk" ]; then
  echo "[Error] missing Android SDK (set ANDROID_SDK_ROOT or install Android Studio SDK)" 1>&2
  exit 2
fi

cmd="$root/src/tooling/build_mobile_export.sh"
args="--with-android-project"
if [ "$name" != "" ]; then
  args="$args --name:$name"
fi
if [ "$out" != "" ]; then
  args="$args --out:$out"
fi
if [ "$assets" != "" ]; then
  args="$args --assets:$assets"
fi
if [ "$plugins" != "" ]; then
  args="$args --plugins:$plugins"
fi
if [ -n "$mm" ]; then
  args="$args --mm:$mm"
fi

echo "[run-android] export: $in" >&2
set -- $args
"$cmd" "$in" "$@"

project="$out/android_project"
if [ ! -f "$project/gradlew" ]; then
  echo "[run-android] gradlew not found: $project/gradlew" 1>&2
  exit 2
fi

verify_script="$root/src/tooling/verify_android_kotlin_only.sh"
sh "$verify_script" --project:"$project"

echo "[run-android] local.properties sdk.dir=$sdk" >&2
printf "sdk.dir=%s\n" "$sdk" > "$project/local.properties"

echo "[run-android] build: $project" >&2
(cd "$project" && ./gradlew assembleDebug)

apk="$project/app/build/outputs/apk/debug/app-debug.apk"
if [ ! -f "$apk" ]; then
  echo "[run-android] debug apk not found: $apk" 1>&2
  exit 1
fi

echo "[run-android] install: $apk" >&2
adb -s "$serial" install -r "$apk" >/dev/null

pkg="com.cheng.mobile"
activity="com.cheng.mobile/.ChengActivity"
echo "[run-android] launch: $pkg ($serial)" >&2
adb -s "$serial" shell am force-stop "$pkg" >/dev/null 2>&1 || true
adb -s "$serial" shell am start -n "$activity" >/dev/null

echo "mobile_run_android ok"
