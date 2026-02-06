#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

scope="${CHENG_STD_IMPORT_SCOPE:-src/stage1 src/backend src/tooling src/decentralized src/web}"
forbidden='^[[:space:]]*import[[:space:]]+cheng/stdlib/bootstrap'
has_error=0

for dir in $scope; do
  [ -e "$dir" ] || continue
  if rg -n "$forbidden" "$dir" --glob '*.cheng' >/tmp/cheng_std_import_surface_hits.txt 2>/dev/null; then
    echo "[verify_std_import_surface] forbidden bootstrap import path found in $dir" 1>&2
    cat /tmp/cheng_std_import_surface_hits.txt 1>&2
    has_error=1
  fi
done

rm -f /tmp/cheng_std_import_surface_hits.txt

if [ "$has_error" -ne 0 ]; then
  exit 1
fi

echo "verify_std_import_surface ok"
