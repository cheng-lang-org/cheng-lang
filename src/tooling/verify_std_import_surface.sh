#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

scope="${STD_IMPORT_SCOPE:-src/stage1 src/backend src/tooling src/decentralized src/web}"
forbidden='^[[:space:]]*import[[:space:]]+cheng/stdlib/bootstrap'
forbidden_reserve_generic='reserve\[[^]]+\][[:space:]]*\('
has_error=0

for dir in $scope; do
  [ -e "$dir" ] || continue
  if rg -n "$forbidden" "$dir" --glob '*.cheng' >/tmp/cheng_std_import_surface_hits.txt 2>/dev/null; then
    echo "[verify_std_import_surface] forbidden bootstrap import path found in $dir" 1>&2
    cat /tmp/cheng_std_import_surface_hits.txt 1>&2
    has_error=1
  fi
done

if rg -n "$forbidden_reserve_generic" src tests examples --glob '*.cheng' >/tmp/cheng_std_import_surface_reserve_hits.txt 2>/dev/null; then
  echo "[verify_std_import_surface] forbidden explicit generic reserve[T](...) call found" 1>&2
  cat /tmp/cheng_std_import_surface_reserve_hits.txt 1>&2
  has_error=1
fi

rm -f /tmp/cheng_std_import_surface_hits.txt
rm -f /tmp/cheng_std_import_surface_reserve_hits.txt

if [ "$has_error" -ne 0 ]; then
  exit 1
fi

echo "verify_std_import_surface ok"
