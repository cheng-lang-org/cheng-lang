#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_backend_dod_contract.sh [--out:<path>] [--doc:<path>]

Notes:
  - Generates deterministic DOPAR-01 DOD contract baseline.
  - Default output: src/tooling/backend_dod_contract.env
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

out="src/tooling/backend_dod_contract.env"
doc="docs/cheng-plan-full.md"

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
  echo "[build_backend_dod_contract] rg is required" 1>&2
  exit 2
fi

stage1_ast="src/stage1/ast.cheng"
uir_types="src/backend/uir/uir_internal/uir_core_types.cheng"
uir_ssa="src/backend/uir/uir_internal/uir_core_ssa.cheng"
verify_script="src/tooling/verify_backend_dod_contract.sh"
verify_dod_soa="src/tooling/verify_backend_dod_soa.sh"
closedloop_script="src/tooling/verify_backend_closedloop.sh"
prod_closure_script="src/tooling/backend_prod_closure.sh"

for required in \
  "$doc" \
  "$stage1_ast" \
  "$uir_types" \
  "$uir_ssa" \
  "$verify_script" \
  "$verify_dod_soa" \
  "$closedloop_script" \
  "$prod_closure_script"; do
  if [ ! -f "$required" ]; then
    echo "[build_backend_dod_contract] missing file: $required" 1>&2
    exit 2
  fi
done

tmp_markers="$(mktemp "${TMPDIR:-/tmp}/backend_dod_contract_markers.XXXXXX")"
cleanup() {
  rm -f "$tmp_markers"
}
trap cleanup EXIT

{
  rg -o \
    'DOD-ATOM-0[1-5]|DOPAR-0[1-2]|backend\.dod_contract|backend\.dod_soa|SoA|Arena|No-Alias|E-Graph' \
    "$doc" || true
  rg -o \
    'nodeArena: Node\[\]|nodeArenaEnabled: bool = true' \
    "$stage1_ast" || true
  rg -o \
    'localIndex: int32|slot: int32|frameOff: int32' \
    "$uir_types" || true
  rg -o \
    'rpoIndex: int32\[\]' \
    "$uir_ssa" || true
} | LC_ALL=C sort -u >"$tmp_markers"

marker_count="$(wc -l < "$tmp_markers" | tr -d ' ')"
if [ "$marker_count" -eq 0 ]; then
  echo "[build_backend_dod_contract] no DOD contract markers found" 1>&2
  exit 2
fi

stage1_arena_count="$(rg -o 'nodeArena: Node\[\]|nodeArenaEnabled: bool = true' "$stage1_ast" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
uir_index_count="$(rg -o 'localIndex: int32|slot: int32|frameOff: int32|rpoIndex: int32\[\]' "$uir_types" "$uir_ssa" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
doc_sha="$(hash_file "$doc")"
marker_sha="$(hash_file "$tmp_markers")"
verify_sha="$(hash_file "$verify_script")"
verify_dod_soa_sha="$(hash_file "$verify_dod_soa")"
closedloop_sha="$(hash_file "$closedloop_script")"
prod_closure_sha="$(hash_file "$prod_closure_script")"

out_dir="$(dirname "$out")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

{
  echo "BACKEND_DOD_CONTRACT_BASELINE_VERSION=1"
  echo "BACKEND_DOD_CONTRACT_DOC=$doc"
  echo "BACKEND_DOD_CONTRACT_DOC_SHA256=$doc_sha"
  echo "BACKEND_DOD_CONTRACT_MARKER_COUNT=$marker_count"
  echo "BACKEND_DOD_CONTRACT_MARKER_SHA256=$marker_sha"
  echo "BACKEND_DOD_CONTRACT_STAGE1_ARENA_MARKER_COUNT=$stage1_arena_count"
  echo "BACKEND_DOD_CONTRACT_UIR_INDEX_MARKER_COUNT=$uir_index_count"
  echo "BACKEND_DOD_CONTRACT_VERIFY_SCRIPT_SHA256=$verify_sha"
  echo "BACKEND_DOD_CONTRACT_VERIFY_DOD_SOA_SHA256=$verify_dod_soa_sha"
  echo "BACKEND_DOD_CONTRACT_CLOSEDLOOP_SHA256=$closedloop_sha"
  echo "BACKEND_DOD_CONTRACT_PROD_CLOSURE_SHA256=$prod_closure_sha"
  echo "BACKEND_DOD_CONTRACT_REQUIRED_GATES=backend.dod_contract,backend.dod_soa"
} >"$out"

echo "backend dod contract baseline generated: $out"
