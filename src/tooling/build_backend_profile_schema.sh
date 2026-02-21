#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_backend_profile_schema.sh [--out:<path>]

Notes:
  - Generates a deterministic profile schema snapshot for PAR-01.
  - Default output: src/tooling/backend_profile_schema.env
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

out="src/tooling/backend_profile_schema.env"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out="${1#--out:}"
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
  echo "[build_backend_profile_schema] rg is required" 1>&2
  exit 2
fi

driver_file="src/backend/tooling/backend_driver.cheng"
uir_builder_file="src/backend/uir/uir_internal/uir_core_builder.cheng"
stage1_file="src/stage1/frontend_lib.cheng"

for required in "$driver_file" "$uir_builder_file" "$stage1_file"; do
  if [ ! -f "$required" ]; then
    echo "[build_backend_profile_schema] missing source file: $required" 1>&2
    exit 2
  fi
done

tmp_markers="$(mktemp "${TMPDIR:-/tmp}/backend_profile_schema_markers.XXXXXX")"
cleanup() {
  rm -f "$tmp_markers"
}
trap cleanup EXIT

marker_regex='backend_profile|uir_profile|generics_report|\[stage1\] profile'
rg --no-filename --no-line-number -o "$marker_regex" \
  "$driver_file" \
  "$uir_builder_file" \
  "$stage1_file" \
  | LC_ALL=C sort >"$tmp_markers"

marker_count="$(wc -l < "$tmp_markers" | tr -d ' ')"
marker_sha="$(hash_file "$tmp_markers")"

out_dir="$(dirname "$out")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

{
  echo "BACKEND_PROFILE_SCHEMA_VERSION=1"
  echo "BACKEND_PROFILE_SCHEMA_PREFIXES=backend_profile,uir_profile,stage1_profile,generics_report"
  echo "BACKEND_PROFILE_GENERICS_FIELDS=ir,mode,spec_budget,borrow_ir,generic_lowering"
  echo "BACKEND_PROFILE_SCHEMA_MARKER_REGEX=backend_profile|uir_profile|generics_report|\\\\[stage1\\\\]\\\\ profile"
  echo "BACKEND_PROFILE_SCHEMA_MARKER_COUNT=$marker_count"
  echo "BACKEND_PROFILE_SCHEMA_MARKER_SHA256=$marker_sha"
  echo "BACKEND_PROFILE_SCHEMA_SOURCES=$driver_file,$uir_builder_file,$stage1_file"
} >"$out"

echo "backend profile schema generated: $out"
