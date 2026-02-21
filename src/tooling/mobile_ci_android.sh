#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/mobile_ci_android.sh <file.cheng> [--name:<appName>] [--out:<dir>] [--assets:<dir>] [--plugins:<csv>] [--mm:<orc>] [--orc]

Builds the Android template and runs ./gradlew assembleDebug.
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

echo "[ci-android] export: $in"
set -- $args
"$cmd" "$in" "$@"

project="$out/android_project"
if [ ! -f "$project/gradlew" ]; then
  echo "[ci-android] gradlew not found: $project/gradlew" 1>&2
  exit 2
fi

verify_script="$root/src/tooling/verify_android_kotlin_only.sh"
sh "$verify_script" --project:"$project"

echo "[ci-android] build: $project"
(cd "$project" && ./gradlew assembleDebug)
echo "[ci-android] ok"
