#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/mobile_ci_ios.sh <file.cheng> [--name:<appName>] [--out:<dir>] [--assets:<dir>] [--plugins:<csv>] [--mm:<orc>] [--orc]

Builds the iOS template and runs xcodegen/xcodebuild if available.
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

if [ "$name" = "" ]; then
  base="$(basename "$in")"
  name="${base%.cheng}"
fi

root="$(cd "$(dirname "$0")/../.." && pwd)"
if [ "$out" = "" ]; then
  out="mobile_build/$name"
fi
case "$out" in
  /*) ;;
  *) out="$root/$out" ;;
esac

cmd="$root/src/tooling/build_mobile_export.sh"
args="--with-ios-project"
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

echo "[ci-ios] export: $in"
set -- $args
"$cmd" "$in" "$@"

project="$out/ios_project"
if [ ! -d "$project" ]; then
  echo "[ci-ios] project not found: $project" 1>&2
  exit 2
fi

if command -v xcodegen >/dev/null 2>&1; then
  echo "[ci-ios] xcodegen"
  (cd "$project" && sh ./generate_xcodeproj.sh)
else
  echo "[ci-ios] xcodegen not found; skip project generation"
fi

if command -v xcodebuild >/dev/null 2>&1; then
  if [ -d "$project/ChengMobileApp.xcodeproj" ]; then
    echo "[ci-ios] xcodebuild"
    (cd "$project" && xcodebuild -project ChengMobileApp.xcodeproj -scheme ChengMobileApp -configuration Debug -sdk iphonesimulator -destination "generic/platform=iOS Simulator" build)
  else
    echo "[ci-ios] xcodeproj missing; run xcodegen first"
  fi
else
  echo "[ci-ios] xcodebuild not found; skip build"
fi

if [ ! -d "$project/ChengMobileApp.xcodeproj" ] || ! command -v xcodebuild >/dev/null 2>&1; then
  if command -v xcrun >/dev/null 2>&1; then
    echo "[ci-ios] fallback clang syntax check"
    src="$project/ChengMobileApp"
    sdk="$(xcrun --sdk iphonesimulator --show-sdk-path)"
    xcrun --sdk iphonesimulator clang -fobjc-arc -fsyntax-only -isysroot "$sdk" -I"$src" "$src/cheng_mobile_ios_glue.m"
    xcrun --sdk iphonesimulator clang -fobjc-arc -fsyntax-only -isysroot "$sdk" -I"$src" "$src/ChengViewController.m"
  else
    echo "[ci-ios] xcrun not found; skip fallback syntax check"
  fi
fi

echo "[ci-ios] ok"
