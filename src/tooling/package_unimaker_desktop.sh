#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu

usage() {
  cat <<'EOF'
Usage:
  src/tooling/package_unimaker_desktop.sh [--out:<dir>] [--name:<prog>] [--compiler:auto|stage1]
                                           [--ide-compiler:auto|stage1] [--ide-bin:<path>] [--skip-ide]
                                           [--no-archive] [--mm:<orc>] [--orc]

Notes:
  - Build Unimaker Desktop and generate a distribution directory.
  - Package the IDE binary and ide/resources by default.
  - Default output is dist/<prog>-<platform>; optionally create .tar.gz.
EOF
}

prog="unimaker_desktop"
out=""
compiler="stage1"
ide_compiler="stage1"
ide_bin=""
skip_ide="0"
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
    --ide-compiler:*)
      ide_compiler="${1#--ide-compiler:}"
      ;;
    --ide-bin:*)
      ide_bin="${1#--ide-bin:}"
      ;;
    --skip-ide)
      skip_ide="1"
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
ide_bin_name="main_local"
case "$platform" in
  windows)
    bin_name="${prog}.exe"
    ide_bin_name="main_local.exe"
    ;;
esac
bin_path="$out/$bin_name"
ide_out="$out/ide/$ide_bin_name"
ide_manifest="$out/ide/manifest.txt"
ide_prev_dir="$out/ide/previous"
ide_prev_manifest="$ide_prev_dir/manifest.txt"

mkdir -p "$out"

mm_flag=""
if [ -n "$mm" ]; then
  mm_flag="--mm:$mm"
fi

"$root/src/tooling/build_unimaker_desktop.sh" --out:"$bin_path" --name:"$prog" --compiler:"$compiler" $mm_flag

if [ "$skip_ide" = "0" ]; then
  mkdir -p "$out/ide"
  if [ -f "$ide_out" ]; then
    mkdir -p "$ide_prev_dir"
    cp -p "$ide_out" "$ide_prev_dir/$ide_bin_name"
  fi
  if [ -d "$out/ide/resources" ]; then
    mkdir -p "$ide_prev_dir"
    rm -rf "$ide_prev_dir/resources"
    cp -R "$out/ide/resources" "$ide_prev_dir/"
  fi
  if [ -f "$ide_manifest" ]; then
    mkdir -p "$ide_prev_dir"
    cp -p "$ide_manifest" "$ide_prev_manifest"
  fi
  if [ -n "$ide_bin" ]; then
    if [ ! -f "$ide_bin" ]; then
      echo "[Error] missing IDE binary: $ide_bin" 1>&2
      exit 2
    fi
    cp -p "$ide_bin" "$ide_out"
    ide_source="copy"
  else
    "$root/src/tooling/build_ide_gui.sh" --out:"$ide_out" --name:"cheng_ide_gui" --compiler:"$ide_compiler" $mm_flag
    ide_source="build"
  fi
  if [ -d "$ide_root/resources" ]; then
    cp -R "$ide_root/resources" "$out/ide/"
  fi
  build_ts="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
  {
    echo "built_at=$build_ts"
    echo "ide_bin=$ide_bin_name"
    echo "ide_compiler=$ide_compiler"
    echo "ide_source=$ide_source"
    if [ -n "$mm" ]; then
      echo "mm=$mm"
    fi
  } > "$ide_manifest"
fi

if [ -f "$root/examples/unimaker_desktop/README.md" ]; then
  cp "$root/examples/unimaker_desktop/README.md" "$out/README.md"
fi
if [ -f "$root/doc/unimaker-desktop-dev-plan.md" ]; then
  cp "$root/doc/unimaker-desktop-dev-plan.md" "$out/"
fi
if [ -f "$root/LICENSE" ]; then
  cp "$root/LICENSE" "$out/"
fi
if [ -f "$root/LICENSE-MIT" ]; then
  cp "$root/LICENSE-MIT" "$out/"
fi
if [ -f "$root/LICENSE-APACHEv2" ]; then
  cp "$root/LICENSE-APACHEv2" "$out/"
fi

echo "[package] out: $out"
echo "[package] bin: $bin_path"
if [ "$skip_ide" = "0" ]; then
  echo "[package] ide: $ide_out"
fi

if [ "$archive" = "1" ]; then
  parent_dir="$(dirname "$out")"
  base_name="$(basename "$out")"
  archive_path="${out}.tar.gz"
  (cd "$parent_dir" && tar -czf "$archive_path" "$base_name")
  echo "[package] archive: $archive_path"
fi
