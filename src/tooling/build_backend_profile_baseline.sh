#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_backend_profile_baseline.sh [--out:<path>] [--schema:<path>]

Notes:
  - Generates deterministic PAR-01 baseline snapshot for profile/plugin closure.
  - Default output: src/tooling/backend_profile_baseline.env
EOF
}

hash_file() {
  file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return
  fi
  cksum "$file" | awk '{print $1}'
}

out="src/tooling/backend_profile_baseline.env"
schema_file="src/tooling/backend_profile_schema.env"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out="${1#--out:}"
      ;;
    --schema:*)
      schema_file="${1#--schema:}"
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

if ! command -v rg >/dev/null 2>&1; then
  echo "[build_backend_profile_baseline] rg is required" 1>&2
  exit 2
fi

for required in \
  "$schema_file" \
  "src/backend/tooling/backend_driver.cheng" \
  "src/tooling/verify_backend_closedloop.sh" \
  "src/tooling/verify_backend_plugin_isolation.sh" \
  "src/tooling/verify_backend_plugin_system.sh" \
  "src/tooling/verify_backend_profile_schema.sh" \
  "src/tooling/verify_backend_profile_baseline.sh"; do
  if [ ! -f "$required" ]; then
    echo "[build_backend_profile_baseline] missing file: $required" 1>&2
    exit 2
  fi
done

tmp_driver_markers="$(mktemp "${TMPDIR:-/tmp}/backend_profile_driver_markers.XXXXXX")"
cleanup() {
  rm -f "$tmp_driver_markers"
}
trap cleanup EXIT

rg --no-filename --no-line-number -o '"backend_profile|uir_profile|generics_report|plugin_enable=|plugin_path=|plugin_paths=|metering_plugin="' \
  src/backend/tooling/backend_driver.cheng \
  | LC_ALL=C sort >"$tmp_driver_markers"

driver_marker_count="$(wc -l < "$tmp_driver_markers" | tr -d ' ')"
driver_marker_sha="$(hash_file "$tmp_driver_markers")"

schema_sha="$(hash_file "$schema_file")"
closedloop_sha="$(hash_file src/tooling/verify_backend_closedloop.sh)"
plugin_isolation_sha="$(hash_file src/tooling/verify_backend_plugin_isolation.sh)"
plugin_system_sha="$(hash_file src/tooling/verify_backend_plugin_system.sh)"
verify_schema_sha="$(hash_file src/tooling/verify_backend_profile_schema.sh)"
verify_baseline_sha="$(hash_file src/tooling/verify_backend_profile_baseline.sh)"

out_dir="$(dirname "$out")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

{
  echo "BACKEND_PROFILE_BASELINE_VERSION=1"
  echo "BACKEND_PROFILE_SCHEMA_FILE=$schema_file"
  echo "BACKEND_PROFILE_SCHEMA_SHA256=$schema_sha"
  echo "BACKEND_PROFILE_DRIVER_MARKER_COUNT=$driver_marker_count"
  echo "BACKEND_PROFILE_DRIVER_MARKER_SHA256=$driver_marker_sha"
  echo "BACKEND_PROFILE_CLOSEDLOOP_SHA256=$closedloop_sha"
  echo "BACKEND_PROFILE_PLUGIN_ISOLATION_SHA256=$plugin_isolation_sha"
  echo "BACKEND_PROFILE_PLUGIN_SYSTEM_SHA256=$plugin_system_sha"
  echo "BACKEND_PROFILE_VERIFY_SCHEMA_SHA256=$verify_schema_sha"
  echo "BACKEND_PROFILE_VERIFY_BASELINE_SHA256=$verify_baseline_sha"
  echo "BACKEND_PROFILE_REQUIRED_GATES=backend.profile_schema,backend.profile_baseline,backend.plugin_isolation,backend.plugin_system"
} >"$out"

echo "backend profile baseline generated: $out"
