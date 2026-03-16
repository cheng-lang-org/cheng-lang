#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_rawptr_contract [--out:<path>] [--doc:<path>] [--formal-doc:<path>] [--tooling-doc:<path>]

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

read_marker_value() {
  file="$1"
  marker_regex="$2"
  rg -o "${marker_regex}=[A-Za-z0-9_]+" "$file" | head -n 1 | cut -d= -f2 || true
}

require_marker_value() {
  file="$1"
  marker_regex="$2"
  marker_name="$3"
  expected="$4"
  value="$(read_marker_value "$file" "$marker_regex")"
  if [ "$value" != "$expected" ]; then
    echo "[build_backend_rawptr_contract] ${marker_name} must be ${expected} in: $file" 1>&2
    exit 2
  fi
  printf '%s\n' "$value"
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

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if ! command -v rg >/dev/null 2>&1; then
  echo "[build_backend_rawptr_contract] rg is required" 1>&2
  exit 2
fi

verify_script="src/tooling/cheng_tooling_embedded_inline.cheng"
closedloop_script="src/tooling/cheng_tooling_embedded_inline.cheng"
prod_closure_script="src/tooling/cheng_tooling_embedded_inline.cheng"

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
tmp_required_gates="$(mktemp "${TMPDIR:-/tmp}/backend_rawptr_contract_required.XXXXXX")"
cleanup() {
  rm -f "$tmp_markers" "$tmp_required_gates"
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

contract_version="$(read_marker_value "$doc" 'rawptr_contract\.version')"
if [ "$contract_version" = "" ]; then
  echo "[build_backend_rawptr_contract] missing rawptr_contract.version marker in: $doc" 1>&2
  exit 2
fi

doc_scheme_id="$(require_marker_value "$doc" 'rawptr_contract\.scheme\.id' 'rawptr_contract.scheme.id' 'ZRPC')"
doc_scheme_name="$(require_marker_value "$doc" 'rawptr_contract\.scheme\.name' 'rawptr_contract.scheme.name' 'zero_rawptr_production_closure')"
doc_scheme_normative="$(require_marker_value "$doc" 'rawptr_contract\.scheme\.normative' 'rawptr_contract.scheme.normative' '1')"
doc_enforce_mode="$(require_marker_value "$doc" 'rawptr_contract\.enforce\.mode' 'rawptr_contract.enforce.mode' 'hard_fail')"

require_marker_value "$formal_doc" 'rawptr_contract\.scheme\.id' 'rawptr_contract.scheme.id' 'ZRPC' >/dev/null
require_marker_value "$formal_doc" 'rawptr_contract\.scheme\.name' 'rawptr_contract.scheme.name' 'zero_rawptr_production_closure' >/dev/null
require_marker_value "$formal_doc" 'rawptr_contract\.scheme\.normative' 'rawptr_contract.scheme.normative' '1' >/dev/null
require_marker_value "$formal_doc" 'rawptr_contract\.enforce\.mode' 'rawptr_contract.enforce.mode' 'hard_fail' >/dev/null

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
rg -o 'rawptr_contract\.required_gate\.[a-z0-9_.]+=1' "$doc" \
  | sed -E 's/^rawptr_contract\.required_gate\.([a-z0-9_.]+)=1$/\1/' \
  | LC_ALL=C sort -u >"$tmp_required_gates"
required_gate_count="$(wc -l < "$tmp_required_gates" | tr -d ' ')"
if [ "$required_gate_count" -eq 0 ]; then
  echo "[build_backend_rawptr_contract] no required rawptr gates found in: $doc" 1>&2
  exit 2
fi
required_gates_csv="$(tr '\n' ',' <"$tmp_required_gates" | sed -E 's/,+$//')"
if [ "$required_gates_csv" = "" ]; then
  echo "[build_backend_rawptr_contract] failed to compute required gate CSV" 1>&2
  exit 2
fi
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
  echo "BACKEND_RAWPTR_CONTRACT_SCHEME_ID=$doc_scheme_id"
  echo "BACKEND_RAWPTR_CONTRACT_SCHEME_NAME=$doc_scheme_name"
  echo "BACKEND_RAWPTR_CONTRACT_SCHEME_NORMATIVE=$doc_scheme_normative"
  echo "BACKEND_RAWPTR_CONTRACT_ENFORCE_MODE=$doc_enforce_mode"
  echo "BACKEND_RAWPTR_CONTRACT_MARKER_COUNT=$marker_count"
  echo "BACKEND_RAWPTR_CONTRACT_MARKER_SHA256=$marker_sha"
  echo "BACKEND_RAWPTR_CONTRACT_ANNOTATION_COUNT=$annotation_count"
  echo "BACKEND_RAWPTR_CONTRACT_FORBID_COUNT=$forbid_count"
  echo "BACKEND_RAWPTR_CONTRACT_REQUIRED_GATE_COUNT=$required_gate_count"
  echo "BACKEND_RAWPTR_CONTRACT_VERIFY_SCRIPT_SHA256=$verify_sha"
  echo "BACKEND_RAWPTR_CONTRACT_CLOSEDLOOP_SHA256=$closedloop_sha"
  echo "BACKEND_RAWPTR_CONTRACT_PROD_CLOSURE_SHA256=$prod_closure_sha"
  echo "BACKEND_RAWPTR_CONTRACT_REQUIRED_GATES=$required_gates_csv"
} >"$out"

echo "backend rawptr contract baseline generated: $out"
