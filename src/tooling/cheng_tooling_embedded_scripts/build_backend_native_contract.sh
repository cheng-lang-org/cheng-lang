#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_native_contract [--out:<path>] [--doc:<path>]

Notes:
  - Generates deterministic CNCPAR-01 native contract baseline.
  - Default output: src/tooling/backend_native_contract.env
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

out_file="src/tooling/backend_native_contract.env"
doc="docs/cheng-native-contract.md"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out_file="${1#--out:}"
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
  echo "[build_backend_native_contract] rg is required" 1>&2
  exit 2
fi

driver_file="v3/src/tooling/compiler_main.cheng"
builder_file="src/backend/uir/uir_internal/uir_core_builder.cheng"
tooling_file="src/tooling/cheng_tooling.cheng"
for required in "$doc" "$driver_file" "$builder_file" "$tooling_file"; do
  if [ ! -f "$required" ]; then
    echo "[build_backend_native_contract] missing file: $required" 1>&2
    exit 2
  fi
done

tmp_markers="$(mktemp "${TMPDIR:-/tmp}/backend_native_contract_markers.XXXXXX")"
tmp_required="$(mktemp "${TMPDIR:-/tmp}/backend_native_contract_required.XXXXXX")"
cleanup() {
  rm -f "$tmp_markers" "$tmp_required"
}
trap cleanup EXIT

rg -o 'native_contract\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$doc" \
  | LC_ALL=C sort -u >"$tmp_markers"
marker_count="$(wc -l < "$tmp_markers" | tr -d ' ')"
if [ "$marker_count" -eq 0 ]; then
  echo "[build_backend_native_contract] no native contract markers found in: $doc" 1>&2
  exit 2
fi

contract_version="$(rg -o 'native_contract\.version=[0-9]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
scheme_id="$(rg -o 'native_contract\.scheme\.id=[A-Za-z0-9_]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
scheme_name="$(rg -o 'native_contract\.scheme\.name=[A-Za-z0-9_]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
scheme_normative="$(rg -o 'native_contract\.scheme\.normative=[0-9]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
enforce_mode="$(rg -o 'native_contract\.enforce\.mode=[A-Za-z0-9_]+' "$doc" | head -n 1 | cut -d= -f2 || true)"
if [ "$contract_version" = "" ] || [ "$scheme_id" = "" ] || [ "$scheme_name" = "" ] || [ "$scheme_normative" = "" ] || [ "$enforce_mode" = "" ]; then
  echo "[build_backend_native_contract] missing required contract markers in: $doc" 1>&2
  exit 2
fi
if [ "$scheme_id" != "CNC" ] || [ "$scheme_name" != "cheng_native_contract" ] || [ "$scheme_normative" != "1" ] || [ "$enforce_mode" != "hard_fail" ]; then
  echo "[build_backend_native_contract] CNC normative markers must be CNC/cheng_native_contract/normative=1/hard_fail" 1>&2
  exit 2
fi

pillar_count="$(rg -o 'native_contract\.pillar\.[a-z0-9_.]+=[A-Za-z0-9_]+' "$doc" | LC_ALL=C sort -u | wc -l | tr -d ' ')"
rg -o 'native_contract\.required_gate\.[a-z0-9_.]+=1' "$doc" \
  | sed -E 's/^native_contract\.required_gate\.([a-z0-9_.]+)=1$/\1/' \
  | LC_ALL=C sort -u >"$tmp_required"
required_gate_count="$(wc -l < "$tmp_required" | tr -d ' ')"
if [ "$required_gate_count" -eq 0 ]; then
  echo "[build_backend_native_contract] no required gates found in: $doc" 1>&2
  exit 2
fi
required_gates_csv="$(tr '\n' ',' <"$tmp_required" | sed -E 's/,+$//')"
if [ "$required_gates_csv" = "" ]; then
  echo "[build_backend_native_contract] failed to compute required gate list" 1>&2
  exit 2
fi

doc_sha="$(hash_file "$doc")"
marker_sha="$(hash_file "$tmp_markers")"
tooling_sha="$(hash_file "$tooling_file")"
driver_sha="$(hash_file "$driver_file")"
builder_sha="$(hash_file "$builder_file")"

out_dir="$(dirname "$out_file")"
if [ "$out_dir" != "" ] && [ ! -d "$out_dir" ]; then
  mkdir -p "$out_dir"
fi

{
  echo "BACKEND_NATIVE_CONTRACT_BASELINE_VERSION=1"
  echo "BACKEND_NATIVE_CONTRACT_DOC=$doc"
  echo "BACKEND_NATIVE_CONTRACT_DOC_SHA256=$doc_sha"
  echo "BACKEND_NATIVE_CONTRACT_VERSION=$contract_version"
  echo "BACKEND_NATIVE_CONTRACT_SCHEME_ID=$scheme_id"
  echo "BACKEND_NATIVE_CONTRACT_SCHEME_NAME=$scheme_name"
  echo "BACKEND_NATIVE_CONTRACT_SCHEME_NORMATIVE=$scheme_normative"
  echo "BACKEND_NATIVE_CONTRACT_ENFORCE_MODE=$enforce_mode"
  echo "BACKEND_NATIVE_CONTRACT_MARKER_COUNT=$marker_count"
  echo "BACKEND_NATIVE_CONTRACT_MARKER_SHA256=$marker_sha"
  echo "BACKEND_NATIVE_CONTRACT_PILLAR_COUNT=$pillar_count"
  echo "BACKEND_NATIVE_CONTRACT_REQUIRED_GATE_COUNT=$required_gate_count"
  echo "BACKEND_NATIVE_CONTRACT_VERIFY_SCRIPT_SHA256=$tooling_sha"
  echo "BACKEND_NATIVE_CONTRACT_CLOSEDLOOP_SHA256=$tooling_sha"
  echo "BACKEND_NATIVE_CONTRACT_PROD_CLOSURE_SHA256=$tooling_sha"
  echo "BACKEND_NATIVE_CONTRACT_DRIVER_SHA256=$driver_sha"
  echo "BACKEND_NATIVE_CONTRACT_UIR_BUILDER_SHA256=$builder_sha"
  echo "BACKEND_NATIVE_CONTRACT_REQUIRED_GATES=$required_gates_csv"
} >"$out_file"

echo "backend native contract baseline generated: $out_file"
