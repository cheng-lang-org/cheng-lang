#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_plugin_isolation] rg is required" 1>&2
  exit 2
fi

scope="${PLUGIN_ISOLATION_SCOPE:-src/stage1 src/backend}"
forbidden_pattern='^[[:space:]]*import[[:space:]]+cheng/(web|decentralized)/'
scan_dirs=""

for candidate in $scope; do
  if [ -d "$candidate" ]; then
    scan_dirs="$scan_dirs $candidate"
  fi
done

if [ "$scan_dirs" = "" ]; then
  echo "[verify_backend_plugin_isolation] no valid scope directories found in PLUGIN_ISOLATION_SCOPE=$scope" 1>&2
  exit 2
fi
scan_dirs="${scan_dirs# }"

out_dir="artifacts/backend_plugin_isolation"
scan_output="$out_dir/plugin_isolation.scan.txt"
snapshot_file="$out_dir/plugin_isolation.snapshot.env"
report_file="$out_dir/plugin_isolation.report.txt"
hash_file="$out_dir/plugin_isolation.scan.sha256"
mkdir -p "$out_dir"

scan_timestamp="$(date -u +'%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo "<unknown>")"
git_head="$(git -C "$root" rev-parse --verify HEAD 2>/dev/null || echo "<unknown>")"
git_status_count="$(git -C "$root" status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
if [ "$git_status_count" = "" ]; then
  git_status_count="0"
fi

set +e
rg -n "$forbidden_pattern" $scan_dirs --glob '*.cheng' > "$scan_output" 2>/dev/null
scan_status="$?"
set -e

case "$scan_status" in
  0|1) ;;
  *)
    echo "[verify_backend_plugin_isolation] rg scan failed" 1>&2
    exit "$scan_status"
    ;;
esac

if [ "$scan_status" -eq 0 ]; then
  total_hits="$(wc -l < "$scan_output" | tr -d ' ')"
else
  total_hits="0"
  : > "$scan_output"
fi

if command -v shasum >/dev/null 2>&1; then
  scan_hash="$(shasum -a 256 "$scan_output" | awk '{print $1}')"
elif command -v sha256sum >/dev/null 2>&1; then
  scan_hash="$(sha256sum "$scan_output" | awk '{print $1}')"
else
  scan_hash="unavailable"
fi
printf '%s\n' "$scan_hash" > "$hash_file"

{
  echo "plugin_isolation_snapshot_time_utc=$scan_timestamp"
  echo "git_head=$git_head"
  echo "git_dirty_files=$git_status_count"
  echo "scan_scope=$scope"
  echo "scan_dirs=$scan_dirs"
  echo "forbidden_pattern=$forbidden_pattern"
  echo "scan_sha256=$scan_hash"
  echo "total_hits=$total_hits"
  echo "scan_output=$scan_output"
} > "$snapshot_file"

{
  echo "verify_backend_plugin_isolation report"
  echo "scope=$scope"
  echo "scan_dirs=$scan_dirs"
  echo "forbidden_pattern=$forbidden_pattern"
  echo "total_hits=$total_hits"
  echo "scan_sha256=$scan_hash"
  if [ "$total_hits" != "0" ]; then
    echo "hits:"
    sed 's/^/  - /' "$scan_output"
  fi
} > "$report_file"

if [ "$total_hits" -ne 0 ]; then
  echo "[verify_backend_plugin_isolation] forbidden plugin imports found under core scope:" 1>&2
  sed 's/^/  - /' "$scan_output" 1>&2
  exit 1
fi

echo "verify_backend_plugin_isolation ok"
