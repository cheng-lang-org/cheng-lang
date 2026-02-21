#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/cleanup_cheng_local.sh [--verbose]

Moves local backend driver binaries and their debug symbol bundles out of the repo
root:
  cheng*
  chengcache/cheng*

Notes:
  - These are local build/debug artifacts (they are gitignored).
  - Safe to run even when nothing matches.
EOF
}

case "${1:-}" in
  --help|-h)
    usage
    exit 0
    ;;
  ""|--verbose)
    ;;
  *)
    echo "[Error] unknown arg: $1" 1>&2
    usage
    exit 2
    ;;
esac

verbose="0"
if [ "${1:-}" = "--verbose" ]; then
  verbose="1"
fi

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"

deleted="0"
trash_dir="${TOOLING_TRASH_DIR:-$root/chengcache/_trash_cheng}"
mkdir -p "$trash_dir" 2>/dev/null || true

move_to_trash() {
  path="$1"
  if [ ! -e "$path" ]; then
    return
  fi
  base="$(basename "$path")"
  ts="$(date +%s 2>/dev/null || echo 0)"
  dst="$trash_dir/${base}.${ts}.$$"
  mv -f -- "$path" "$dst" 2>/dev/null || true
  deleted="1"
}

for path in \
  "$root"/cheng \
  "$root"/cheng.* \
  "$root"/chengcache/cheng*.c \
  "$root"/chengcache/cheng*.c.key \
  "$root"/chengcache/cheng*.build.key \
  "$root"/chengcache/cheng*.deps.list \
  "$root"/chengcache/cheng*.modules; do
  move_to_trash "$path"
done

if [ "$verbose" = "1" ]; then
  if [ "$deleted" = "1" ]; then
    echo "cleanup_cheng_local ok"
  else
    echo "cleanup_cheng_local noop"
  fi
fi
