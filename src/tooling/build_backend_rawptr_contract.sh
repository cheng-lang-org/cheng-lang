#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/build_backend_rawptr_contract.sh [--out:<path>] [--doc:<path>] [--formal-doc:<path>] [--tooling-doc:<path>]

Notes:
  - Generates deterministic RPSPAR-01 raw pointer safety contract baseline.
  - Default output: src/tooling/backend_rawptr_contract.env
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

out="src/tooling/backend_rawptr_contract.env"
doc="docs/raw-pointer-safety.md"
formal_doc="docs/cheng-formal-spec.md"
tooling_doc="src/tooling/README.md"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out="${1#--out:}"
      ;;
    --doc:*)
      doc="${1#--doc:}"
      ;;
    --formal-doc:*)
      formal_doc="${1#--formal-doc:}"
      ;;
    --tooling-doc:*)
      tooling_doc="${1#--tooling-doc:}"
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
  echo "[build_backend_rawptr_contract] rg is required" 1>&2
  exit 2
fi

verify_script="src/tooling/verify_backend_rawptr_contract.sh"
closedloop_script="src/tooling/verify_backend_closedloop.sh"
prod_closure_script="src/tooling/backend_prod_closure.sh"

for required in \
  "$doc" \
  "$formal_doc" \
  "$tooling_doc" \
  "$verify_script" \
  "$closedloop_script" \
  "$prod_closure_script"; do
  if [ ! -f "$required" ]; then
    echo "[build_backend_rawptr_contract] missing file: $required" 1>&2
    exit 2
  fi
done

tmp_markers="$(mktemp "${TMPDIR:-/tmp}/backend_rawptr_contract_markers.XXXXXX")"
cleanup() {
  rm -f "$tmp_markers"
}
trap cleanup EXIT

{
  rg -o 'rawptr_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$doc" || true
  rg -o 'rawptr_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$formal_doc" || true
  rg -o 'rawptr_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$tooling_doc" || true
} | LC_ALL=C sort -u >"$tmp_markers"

marker_count="$(wc -l < "$tmp_markers" | tr -d ' ')"
if [ "$marker_count" -eq 0 ]; then
  echo "[build_backend_rawptr_contract] no rawptr contract markers found" 1>&2
  exit 2
fi

contract_version="$(rg -o 'rawptr_contract\.version=[0-9]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
if [ "$contract_version" = "" ]; then
  echo "[build_backend_rawptr_contract] missing rawptr_contract.version marker in: $doc" 1>&2
  exit 2
fi

if ! rg -q 'rawptr_contract\.formal_spec\.synced=1' "$formal_doc"; then
  echo "[build_backend_rawptr_contract] missing formal spec sync marker: $formal_doc" 1>&2
  exit 2
fi
if ! rg -q 'rawptr_contract\.tooling_readme\.synced=1' "$tooling_doc"; then
  echo "[build_backend_rawptr_contract] missing tooling README sync marker: $tooling_doc" 1>&2
  exit 2
fi

annotation_count="$(rg -o 'rawptr_contract\.annotation\.[a-z0-9_]+=[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
forbid_count="$(rg -o 'rawptr_contract\.forbid\.[a-z0-9_]+=[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
required_gate_count="$(rg -o 'rawptr_contract\.required_gate\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
marker_sha="$(hash_file "$tmp_markers")"
doc_sha="$(hash_file "$doc")"
formal_doc_sha="$(hash_file "$formal_doc")"
tooling_doc_sha="$(hash_file "$tooling_doc")"
verify_sha="$(hash_file "$verify_script")"
closedloop_sha="$(hash_file "$closedloop_script")"
prod_closure_sha="$(hash_file "$prod_closure_script")"

out_dir="$(dirname "$out")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

{
  echo "BACKEND_RAWPTR_CONTRACT_BASELINE_VERSION=1"
  echo "BACKEND_RAWPTR_CONTRACT_DOC=$doc"
  echo "BACKEND_RAWPTR_CONTRACT_FORMAL_DOC=$formal_doc"
  echo "BACKEND_RAWPTR_CONTRACT_TOOLING_DOC=$tooling_doc"
  echo "BACKEND_RAWPTR_CONTRACT_DOC_SHA256=$doc_sha"
  echo "BACKEND_RAWPTR_CONTRACT_FORMAL_DOC_SHA256=$formal_doc_sha"
  echo "BACKEND_RAWPTR_CONTRACT_TOOLING_DOC_SHA256=$tooling_doc_sha"
  echo "BACKEND_RAWPTR_CONTRACT_VERSION=$contract_version"
  echo "BACKEND_RAWPTR_CONTRACT_MARKER_COUNT=$marker_count"
  echo "BACKEND_RAWPTR_CONTRACT_MARKER_SHA256=$marker_sha"
  echo "BACKEND_RAWPTR_CONTRACT_ANNOTATION_COUNT=$annotation_count"
  echo "BACKEND_RAWPTR_CONTRACT_FORBID_COUNT=$forbid_count"
  echo "BACKEND_RAWPTR_CONTRACT_REQUIRED_GATE_COUNT=$required_gate_count"
  echo "BACKEND_RAWPTR_CONTRACT_VERIFY_SCRIPT_SHA256=$verify_sha"
  echo "BACKEND_RAWPTR_CONTRACT_CLOSEDLOOP_SHA256=$closedloop_sha"
  echo "BACKEND_RAWPTR_CONTRACT_PROD_CLOSURE_SHA256=$prod_closure_sha"
  echo "BACKEND_RAWPTR_CONTRACT_REQUIRED_GATES=backend.rawptr_contract,backend.rawptr_surface_forbid"
} >"$out"

echo "backend rawptr contract baseline generated: $out"
