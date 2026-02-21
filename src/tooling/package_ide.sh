#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

usage() {
  cat <<'EOF'
Usage:
  src/tooling/package_ide.sh [--out:<dir>] [--name:<prog>] [--compiler:auto|stage1] [--no-archive] [--mm:<orc>] [--orc]

Notes:
  - Build the native GUI IDE and generate a distribution directory.
  - Default output is dist/<prog>-<platform>; optionally create .tar.gz.
EOF
}

prog="cheng_ide_gui"
out=""
compiler="stage1"
archive="1"
mm=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --name:*)
      prog="${1#--name:}"
      ;;
    --compiler:*)
      compiler="${1#--compiler:}"
      ;;
    --no-archive)
      archive="0"
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

here="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
root="$(CDPATH= cd -- "$here/../.." && pwd)"
ide_root="${IDE_ROOT:-}"
if [ -z "$ide_root" ] && [ -d "$root/../cheng-ide" ]; then
  ide_root="$root/../cheng-ide"
fi
if [ -z "$ide_root" ] || [ ! -d "$ide_root" ]; then
  echo "[Error] missing IDE root (set IDE_ROOT to /path/to/cheng-ide)" 1>&2
  exit 2
fi
export IDE_ROOT="$ide_root"

uname_s="$(uname -s)"
platform="unknown"
case "$uname_s" in
  Darwin)
    platform="macos"
    ;;
  Linux)
    platform="linux"
    ;;
  MINGW*|MSYS*|CYGWIN*|Windows_NT)
    platform="windows"
    ;;
esac

if [ "$out" = "" ]; then
  out="$root/dist/${prog}-${platform}"
fi

bin_name="$prog"
case "$platform" in
  windows)
    bin_name="${prog}.exe"
    ;;
esac
bin_path="$out/$bin_name"

mkdir -p "$out"

mm_flag=""
if [ -n "$mm" ]; then
  mm_flag="--mm:$mm"
fi

"$root/src/tooling/build_ide_gui.sh" --out:"$bin_path" --name:"$prog" --compiler:"$compiler" $mm_flag

if [ -f "$ide_root/README.md" ]; then
  cp "$ide_root/README.md" "$out/"
fi
if [ -f "$ide_root/license.txt" ]; then
  cp "$ide_root/license.txt" "$out/"
fi
if [ -f "$ide_root/docs/cheng-ide-dev-plan.md" ]; then
  cp "$ide_root/docs/cheng-ide-dev-plan.md" "$out/"
fi
if [ -f "$ide_root/resources/fonts/codicon.ttf" ]; then
  mkdir -p "$out/resources"
  cp "$ide_root/resources/fonts/codicon.ttf" "$out/resources/codicon.ttf"
fi

echo "[package] out: $out"
echo "[package] bin: $bin_path"

if [ "$archive" = "1" ]; then
  parent_dir="$(dirname "$out")"
  base_name="$(basename "$out")"
  archive_path="${out}.tar.gz"
  (cd "$parent_dir" && tar -czf "$archive_path" "$base_name")
  echo "[package] archive: $archive_path"
fi
