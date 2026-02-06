#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/prewarm_deps_parallel.sh <entry.cheng>

Env:
  CHENG_DEPS_FRONTEND   Frontend path (default: stage1_runner)
  CHENG_DEPS_JOBS       Parallel jobs (default: 4)
  CHENG_DEPS_ROOTS      Comma-separated roots to scan (default: entry dir + CHENG_PKG_ROOTS + CHENG_GUI_ROOT + CHENG_IDE_ROOT)
  CHENG_DEPS_CACHE_DIR  Output directory for .deps.list (default: chengcache/deps_prewarm)
EOF
}

if [ "${1:-}" = "" ] || [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

entry="$1"
if [ ! -f "$entry" ]; then
  echo "[Error] entry not found: $entry" 1>&2
  exit 2
fi

root="$(cd "$(dirname "$0")/../.." && pwd)"

frontend="${CHENG_DEPS_FRONTEND:-}"
if [ -z "$frontend" ]; then
  if [ -x "$root/stage1_runner" ]; then
    frontend="$root/stage1_runner"
  else
    echo "[Error] deps frontend not found (stage1_runner required)" 1>&2
    exit 2
  fi
fi

jobs="${CHENG_DEPS_JOBS:-4}"
cache_dir="${CHENG_DEPS_CACHE_DIR:-$root/chengcache/deps_prewarm}"
mkdir -p "$cache_dir"

roots="${CHENG_DEPS_ROOTS:-}"
if [ -z "$roots" ]; then
  entry_root="$(CDPATH= cd -- "$(dirname -- "$entry")" && pwd)"
  roots="$entry_root"
  if [ -n "${CHENG_PKG_ROOTS:-}" ]; then
    roots="$roots,${CHENG_PKG_ROOTS}"
  fi
  if [ -n "${CHENG_GUI_ROOT:-}" ]; then
    roots="$roots,${CHENG_GUI_ROOT}"
  fi
  if [ -n "${CHENG_IDE_ROOT:-}" ]; then
    roots="$roots,${CHENG_IDE_ROOT}"
  fi
fi

files_list="$cache_dir/files.list"
rm -f "$files_list"

IFS=',' 
for r in $roots; do
  if [ -z "$r" ] || [ ! -d "$r" ]; then
    continue
  fi
  find "$r" -type f -name '*.cheng' -print0 >> "$files_list"
done
unset IFS

if [ ! -s "$files_list" ]; then
  echo "[Warn] no .cheng files found for deps prewarm" 1>&2
  exit 0
fi

tr '\0' '\n' < "$files_list" | sort -u | tr '\n' '\0' > "$files_list.sorted"
mv "$files_list.sorted" "$files_list"

file_count="$(tr '\0' '\n' < "$files_list" | wc -l | tr -d ' ')"
echo "[prewarm] deps: $file_count files, jobs=$jobs" 1>&2

tr '\0' '\n' < "$files_list" | xargs -P "$jobs" -I{} sh -c '
  file="$1"
  if [ -z "$file" ]; then
    exit 0
  fi
  hash="$(printf "%s" "$file" | cksum | awk "{print \$1}")"
  out="$2/$hash.deps.list"
  CHENG_DEPS_RESOLVE=0 "$3" --mode:deps-list --file:"$file" --out:"$out" >/dev/null 2>&1 || true
' _ {} "$cache_dir" "$frontend"
