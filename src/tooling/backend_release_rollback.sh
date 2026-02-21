#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_release_rollback.sh [--to:<release_id>] [--root:<dir>]

Notes:
  - Switches the current symlink to a previous published release.
  - Default root is dist/releases.
  - If --to is omitted, rolls back to the previous release by directory order.
EOF
}

to=""
root_dir="dist/releases"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --to:*)
      to="${1#--to:}"
      ;;
    --root:*)
      root_dir="${1#--root:}"
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

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

current_link="$root_dir/current"
current_id_file="$root_dir/current_id.txt"
releases_dir="$root_dir"
if [ ! -d "$releases_dir" ]; then
  echo "[Error] missing release root: $releases_dir" 1>&2
  exit 2
fi

current_id=""
if [ -L "$current_link" ]; then
  cur_path="$(readlink "$current_link" 2>/dev/null || echo "")"
  current_id="$(basename "$cur_path")"
fi

pick_prev() {
  python3 - "$1" "$2" <<'PY'
import os, sys
releases = sys.argv[1]
current = sys.argv[2]
items = []
for d in os.listdir(releases):
    if d in ("current", "releases"):
        continue
    p = os.path.join(releases, d)
    if os.path.islink(p):
        continue
    if os.path.isdir(p):
        items.append(d)
items.sort()
if not items:
    print("")
    sys.exit(0)
if current and current in items:
    idx = items.index(current)
    if idx > 0:
        print(items[idx - 1])
        sys.exit(0)
print(items[-1])
PY
}

if [ "$to" = "" ]; then
  to="$(pick_prev "$releases_dir" "$current_id")"
fi
if [ "$to" = "" ]; then
  echo "[Error] no releases found" 1>&2
  exit 2
fi

target="$releases_dir/$to"
if [ ! -d "$target" ]; then
  echo "[Error] release not found: $target" 1>&2
  exit 2
fi

ln -sfn "$to" "$current_link"
printf "%s\n" "$to" >"$current_id_file"

echo "backend release rollback ok: $to"
