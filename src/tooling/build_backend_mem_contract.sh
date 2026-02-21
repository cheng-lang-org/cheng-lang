#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_backend_mem_contract.sh [--out:<path>] [--doc:<path>]

Notes:
  - Generates deterministic PAR-01 Memory-Exe + Hotpatch contract baseline.
  - Default output: src/tooling/backend_mem_contract.env
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

out="src/tooling/backend_mem_contract.env"
doc="docs/backend-mem-hotpatch-contract.md"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out="${1#--out:}"
      ;;
    --doc:*)
      doc="${1#--doc:}"
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
  echo "[build_backend_mem_contract] rg is required" 1>&2
  exit 2
fi

for required in \
  "$doc" \
  "src/tooling/verify_backend_mem_contract.sh" \
  "src/tooling/verify_backend_closedloop.sh" \
  "src/tooling/backend_prod_closure.sh"; do
  if [ ! -f "$required" ]; then
    echo "[build_backend_mem_contract] missing file: $required" 1>&2
    exit 2
  fi
done

tmp_markers="$(mktemp "${TMPDIR:-/tmp}/backend_mem_contract_markers.XXXXXX")"
cleanup() {
  rm -f "$tmp_markers"
}
trap cleanup EXIT

rg -o \
  'mem_contract\.version=[0-9]+|mem_image\.schema\.version=[0-9]+|patch_meta\.schema\.version=[0-9]+|mem_image\.field\.[A-Za-z0-9_]+|patch_meta\.field\.[A-Za-z0-9_]+|mem_image\.invariant\.[A-Za-z0-9_]+|patch_meta\.invariant\.[A-Za-z0-9_]+' \
  "$doc" \
  | LC_ALL=C sort -u >"$tmp_markers"

marker_count="$(wc -l < "$tmp_markers" | tr -d ' ')"
if [ "$marker_count" -eq 0 ]; then
  echo "[build_backend_mem_contract] no contract markers found in: $doc" 1>&2
  exit 2
fi

contract_version="$(rg -o 'mem_contract\.version=[0-9]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
mem_schema_version="$(rg -o 'mem_image\.schema\.version=[0-9]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
patch_schema_version="$(rg -o 'patch_meta\.schema\.version=[0-9]+' "$doc" | head -n 1 | cut -d= -f2 || true)"

if [ "$contract_version" = "" ] || [ "$mem_schema_version" = "" ] || [ "$patch_schema_version" = "" ]; then
  echo "[build_backend_mem_contract] missing version markers in: $doc" 1>&2
  exit 2
fi

mem_field_count="$(rg -o 'mem_image\.field\.[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
patch_field_count="$(rg -o 'patch_meta\.field\.[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
mem_invariant_count="$(rg -o 'mem_image\.invariant\.[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
patch_invariant_count="$(rg -o 'patch_meta\.invariant\.[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"

marker_sha="$(hash_file "$tmp_markers")"
doc_sha="$(hash_file "$doc")"
verify_sha="$(hash_file src/tooling/verify_backend_mem_contract.sh)"
closedloop_sha="$(hash_file src/tooling/verify_backend_closedloop.sh)"
prod_closure_sha="$(hash_file src/tooling/backend_prod_closure.sh)"

out_dir="$(dirname "$out")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

{
  echo "BACKEND_MEM_CONTRACT_BASELINE_VERSION=1"
  echo "BACKEND_MEM_CONTRACT_DOC=$doc"
  echo "BACKEND_MEM_CONTRACT_DOC_SHA256=$doc_sha"
  echo "BACKEND_MEM_CONTRACT_VERSION=$contract_version"
  echo "BACKEND_MEM_IMAGE_SCHEMA_VERSION=$mem_schema_version"
  echo "BACKEND_PATCH_META_SCHEMA_VERSION=$patch_schema_version"
  echo "BACKEND_MEM_CONTRACT_MARKER_COUNT=$marker_count"
  echo "BACKEND_MEM_CONTRACT_MARKER_SHA256=$marker_sha"
  echo "BACKEND_MEM_IMAGE_FIELD_COUNT=$mem_field_count"
  echo "BACKEND_PATCH_META_FIELD_COUNT=$patch_field_count"
  echo "BACKEND_MEM_IMAGE_INVARIANT_COUNT=$mem_invariant_count"
  echo "BACKEND_PATCH_META_INVARIANT_COUNT=$patch_invariant_count"
  echo "BACKEND_MEM_CONTRACT_VERIFY_SCRIPT_SHA256=$verify_sha"
  echo "BACKEND_MEM_CONTRACT_CLOSEDLOOP_SHA256=$closedloop_sha"
  echo "BACKEND_MEM_CONTRACT_PROD_CLOSURE_SHA256=$prod_closure_sha"
  echo "BACKEND_MEM_CONTRACT_REQUIRED_GATES=backend.mem_contract"
} >"$out"

echo "backend mem contract baseline generated: $out"
