#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/mobile_ci_harmony.sh <file.cheng> [--name:<appName>] [--out:<dir>] [--assets:<dir>] [--plugins:<csv>] [--mm:<orc>] [--orc]

Builds the Harmony template and runs native C static smoke checks.
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
args="--with-harmony-project"
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

echo "[ci-harmony] export: $in"
set -- $args
"$cmd" "$in" "$@"

project="$out/harmony_project"
if [ ! -d "$project" ]; then
  echo "[ci-harmony] project not found: $project" 1>&2
  exit 2
fi

required_files="
$project/CMakeLists.txt
$project/cheng_mobile_harmony_bridge.c
$project/cheng_mobile_host_harmony.c
$project/cheng_mobile_host_api.c
$project/cheng_mobile_host_core.c
$project/native/cheng_mobile_harmony_napi.c
"
for f in $required_files; do
  if [ ! -f "$f" ]; then
    echo "[ci-harmony] missing required file: $f" 1>&2
    exit 2
  fi
done

echo "[ci-harmony] native static check"
cc -I"$project" -Wall -Wextra -Werror -fsyntax-only "$project/cheng_mobile_harmony_bridge.c"
cc -I"$project" -Wall -Wextra -Werror -fsyntax-only "$project/cheng_mobile_host_harmony.c"
cc -I"$project" -Wall -Wextra -Werror -fsyntax-only "$project/cheng_mobile_host_api.c"
cc -I"$project" -Wall -Wextra -Werror -fsyntax-only "$project/cheng_mobile_host_core.c"

if command -v cmake >/dev/null 2>&1; then
  echo "[ci-harmony] cmake build"
  build_dir="$project/.cmake-smoke"
  rm -rf "$build_dir"
  cmake -S "$project" -B "$build_dir" >/dev/null
  cmake --build "$build_dir" >/dev/null
fi

if ! rg -n "pollBusRequest|pushBusResponse" "$project/native/cheng_mobile_harmony_napi.c" >/dev/null 2>&1; then
  echo "[ci-harmony] NAPI bus hooks missing in native/cheng_mobile_harmony_napi.c" 1>&2
  exit 2
fi

echo "[ci-harmony] ok"
