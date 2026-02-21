#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

scope="src tests examples"
has_error=0

check_forbidden() {
  pattern="$1"
  label="$2"
  for dir in $scope; do
    [ -e "$dir" ] || continue
    if rg -n "$pattern" "$dir" --glob '*.cheng' >/tmp/cheng_legacy_prefix_hits.txt 2>/dev/null; then
      echo "[verify_no_legacy_net_multiformats_imports] forbidden import found: $label" 1>&2
      cat /tmp/cheng_legacy_prefix_hits.txt 1>&2
      has_error=1
    fi
  done
}

check_forbidden '^[[:space:]]*import[[:space:]]+cheng/net/' 'import cheng/net/...'
check_forbidden '^[[:space:]]*import[[:space:]]+cheng/multiformats/' 'import cheng/multiformats/...'

rm -f /tmp/cheng_legacy_prefix_hits.txt

if [ "$has_error" -ne 0 ]; then
  exit 1
fi

echo "verify_no_legacy_net_multiformats_imports ok"
