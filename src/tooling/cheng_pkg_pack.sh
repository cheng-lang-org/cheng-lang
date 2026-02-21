#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
usage: src/tooling/cheng_pkg_pack.sh --src:<dir> --out:<file> [--exclude:<pattern>...]

Notes:
  - Packs a package directory into a .tar.gz snapshot.
  - Default excludes: .git, chengcache, .cheng-cache, build, dist, out, tmp, tmp_build, __pycache__, .DS_Store
  - Set PKG_EXCLUDE_DEFAULTS=0 to disable default excludes.
EOF
}

src=""
out=""
extra_excludes=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --src:*)
      src="${1#--src:}"
      ;;
    --out:*)
      out="${1#--out:}"
      ;;
    --exclude:*)
      extra_excludes="$extra_excludes --exclude=${1#--exclude:}"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

if [ "$src" = "" ] || [ "$out" = "" ]; then
  echo "[Error] missing --src or --out" 1>&2
  usage
  exit 2
fi

if [ ! -d "$src" ]; then
  echo "[Error] src dir not found: $src" 1>&2
  exit 2
fi

if ! command -v tar >/dev/null 2>&1; then
  echo "[Error] tar not found" 1>&2
  exit 2
fi

out_dir="$(dirname "$out")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

exclude_args=""
if [ "${PKG_EXCLUDE_DEFAULTS:-1}" != "0" ]; then
  exclude_args="--exclude=.git --exclude=chengcache --exclude=.cheng-cache --exclude=build --exclude=dist --exclude=out --exclude=tmp --exclude=tmp_build --exclude=__pycache__ --exclude=.DS_Store"
fi

echo "[pkg] pack: $src -> $out"
tar -czf "$out" $exclude_args $extra_excludes -C "$src" .
