#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
src_dir="${1:-$root/v3/src}"

if [ ! -d "$src_dir" ]; then
  echo "v3 scan: source dir not found: $src_dir" >&2
  exit 1
fi

check_pattern() {
  pattern="$1"
  label="$2"
  if rg -n -S --glob '*.cheng' "$pattern" "$src_dir" >/tmp/v3_scan_hit.$$ 2>&1; then
    echo "v3 scan: hit forbidden pattern: $label" >&2
    cat /tmp/v3_scan_hit.$$ >&2
    rm -f /tmp/v3_scan_hit.$$
    exit 1
  fi
  rm -f /tmp/v3_scan_hit.$$
}

check_pattern 'local_payload' 'local_payload'
check_pattern 'exec_plan_payload' 'exec_plan_payload'
check_pattern 'entry_bridge' 'entry_bridge'
check_pattern 'payloadText' 'payloadText'
check_pattern 'runtimeTarget:[[:space:]]*str' 'runtimeTarget: str'
check_pattern 'opName:[[:space:]]*str' 'opName: str'
check_pattern 'kind:[[:space:]]*str' 'kind: str'
check_pattern '\bBigInt\b' 'BigInt'

echo "v3 scan: ok"
