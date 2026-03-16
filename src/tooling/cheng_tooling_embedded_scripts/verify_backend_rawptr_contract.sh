#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify_backend_rawptr_contract [--baseline:<path>] [--doc:<path>] [--formal-doc:<path>] [--tooling-doc:<path>]

Notes:
  - Verifies RPSPAR-01 raw pointer safety contract freeze baseline.
  - Regenerate baseline with: ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_rawptr_contract
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

read_env_value() {
  key="$1"
  file="$2"
  awk -F= -v k="$key" '
    $1 == k {
      sub(/^[^=]*=/, "", $0)
      print $0
      found=1
      exit
    }
    END {
      if (!found) print ""
    }
  ' "$file"
}

baseline="src/tooling/backend_rawptr_contract.env"
doc="docs/raw-pointer-safety.md"
formal_doc="docs/cheng-formal-spec.md"
tooling_doc="src/tooling/README.md"
prod_closure_file="src/tooling/cheng_tooling_embedded_inline.cheng"
prod_closure_inline_file="src/tooling/cheng_tooling_embedded_inline.cheng"
embedded_map="src/tooling/cheng_tooling_embedded_inline.cheng"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --baseline:*)
      baseline="${1#--baseline:}"
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

if [ ! -f "$baseline" ]; then
  echo "[verify_backend_rawptr_contract] missing baseline file: $baseline" 1>&2
  exit 2
fi
if [ ! -f "$doc" ]; then
  echo "[verify_backend_rawptr_contract] missing doc file: $doc" 1>&2
  exit 2
fi
if [ ! -f "$formal_doc" ]; then
  echo "[verify_backend_rawptr_contract] missing formal doc file: $formal_doc" 1>&2
  exit 2
fi
if [ ! -f "$tooling_doc" ]; then
  echo "[verify_backend_rawptr_contract] missing tooling doc file: $tooling_doc" 1>&2
  exit 2
fi
if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_rawptr_contract] rg is required" 1>&2
  exit 2
fi

# backend_prod_closure.sh may be a thin wrapper; prefer the inline body when available.
if [ -f "$prod_closure_inline_file" ] && ! rg -q 'run_required "' "$prod_closure_file"; then
  prod_closure_file="$prod_closure_inline_file"
fi

out_dir="artifacts/backend_rawptr_contract"
mkdir -p "$out_dir"
prod_closure_body="$out_dir/backend_prod_closure.body.sh"

generated="$out_dir/backend_rawptr_contract.generated.env"
report="$out_dir/backend_rawptr_contract.report.txt"
snapshot="$out_dir/backend_rawptr_contract.snapshot.env"
diff_file="$out_dir/backend_rawptr_contract.diff.txt"

${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_prod_closure --help >"$prod_closure_body" 2>/dev/null || true
if [ ! -s "$prod_closure_body" ]; then
  echo "[verify_backend_rawptr_contract] failed to extract backend_prod_closure body" 1>&2
  exit 2
fi

${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_rawptr_contract \
  --doc:"$doc" \
  --formal-doc:"$formal_doc" \
  --tooling-doc:"$tooling_doc" \
  --out:"$generated" >/dev/null

status="ok"
if ! cmp -s "$baseline" "$generated"; then
  status="drift"
  if diff -u "$baseline" "$generated" >"$diff_file" 2>/dev/null; then
    :
  else
    diff "$baseline" "$generated" >"$diff_file" 2>/dev/null || true
  fi
else
  : >"$diff_file"
fi

closedloop_gate_ok="1"
prod_closure_gate_ok="1"
required_gates_csv="$(read_env_value "BACKEND_RAWPTR_CONTRACT_REQUIRED_GATES" "$generated")"
if [ "$required_gates_csv" = "" ]; then
  required_gates_csv="$(read_env_value "BACKEND_RAWPTR_CONTRACT_REQUIRED_GATES" "$baseline")"
fi
required_gate_count="0"
closedloop_missing=""
prod_closure_missing=""
old_ifs="$IFS"
IFS=','
for gate in $required_gates_csv; do
  gate_clean="$(printf '%s' "$gate" | tr -d '[:space:]')"
  if [ "$gate_clean" = "" ]; then
    continue
  fi
  required_gate_count=$((required_gate_count + 1))
  if ! rg -q 'if id == "verify_backend_closedloop":' src/tooling/cheng_tooling_embedded_inline.cheng; then
    closedloop_gate_ok="0"
    if [ "$closedloop_missing" = "" ]; then
      closedloop_missing="verify_backend_closedloop"
    fi
  fi
  if ! rg -q 'tooling_strEq\(first, "backend_prod_closure"\)' src/tooling/cheng_tooling.cheng || ! rg -q 'tooling_strEq\(selfName, "backend_prod_closure"\)' src/tooling/cheng_tooling.cheng; then
    prod_closure_gate_ok="0"
    if [ "$prod_closure_missing" = "" ]; then
      prod_closure_missing="$gate_clean"
    else
      prod_closure_missing="$prod_closure_missing,$gate_clean"
    fi
  fi
done
IFS="$old_ifs"
if [ "$required_gate_count" -eq 0 ]; then
  closedloop_gate_ok="0"
  prod_closure_gate_ok="0"
fi
if [ "$closedloop_gate_ok" != "1" ] || [ "$prod_closure_gate_ok" != "1" ]; then
  status="drift"
fi

scheme_id_ok="1"
scheme_name_ok="1"
scheme_normative_ok="1"
enforce_mode_ok="1"

if ! rg -q 'rawptr_contract\.scheme\.id=ZRPC' "$doc" || ! rg -q 'rawptr_contract\.scheme\.id=ZRPC' "$formal_doc"; then
  scheme_id_ok="0"
fi
if ! rg -q 'rawptr_contract\.scheme\.name=zero_rawptr_production_closure' "$doc" || ! rg -q 'rawptr_contract\.scheme\.name=zero_rawptr_production_closure' "$formal_doc"; then
  scheme_name_ok="0"
fi
if ! rg -q 'rawptr_contract\.scheme\.normative=1' "$doc" || ! rg -q 'rawptr_contract\.scheme\.normative=1' "$formal_doc"; then
  scheme_normative_ok="0"
fi
if ! rg -q 'rawptr_contract\.enforce\.mode=hard_fail' "$doc" || ! rg -q 'rawptr_contract\.enforce\.mode=hard_fail' "$formal_doc"; then
  enforce_mode_ok="0"
fi

generated_scheme_id="$(read_env_value "BACKEND_RAWPTR_CONTRACT_SCHEME_ID" "$generated")"
generated_scheme_name="$(read_env_value "BACKEND_RAWPTR_CONTRACT_SCHEME_NAME" "$generated")"
generated_scheme_normative="$(read_env_value "BACKEND_RAWPTR_CONTRACT_SCHEME_NORMATIVE" "$generated")"
generated_enforce_mode="$(read_env_value "BACKEND_RAWPTR_CONTRACT_ENFORCE_MODE" "$generated")"

if [ "$generated_scheme_id" != "ZRPC" ]; then
  scheme_id_ok="0"
fi
if [ "$generated_scheme_name" != "zero_rawptr_production_closure" ]; then
  scheme_name_ok="0"
fi
if [ "$generated_scheme_normative" != "1" ]; then
  scheme_normative_ok="0"
fi
if [ "$generated_enforce_mode" != "hard_fail" ]; then
  enforce_mode_ok="0"
fi

if [ "$scheme_id_ok" != "1" ] || [ "$scheme_name_ok" != "1" ] || [ "$scheme_normative_ok" != "1" ] || [ "$enforce_mode_ok" != "1" ]; then
  status="drift"
fi

baseline_sha="$(hash_file "$baseline")"
generated_sha="$(hash_file "$generated")"

{
  echo "verify_backend_rawptr_contract report"
  echo "status=$status"
  echo "doc=$doc"
  echo "formal_doc=$formal_doc"
  echo "tooling_doc=$tooling_doc"
  echo "baseline=$baseline"
  echo "generated=$generated"
  echo "baseline_sha256=$baseline_sha"
  echo "generated_sha256=$generated_sha"
  echo "required_gates_csv=$required_gates_csv"
  echo "required_gate_count=$required_gate_count"
  echo "closedloop_gate_ok=$closedloop_gate_ok"
  echo "prod_closure_gate_ok=$prod_closure_gate_ok"
  echo "closedloop_missing=$closedloop_missing"
  echo "prod_closure_missing=$prod_closure_missing"
  echo "scheme_id_ok=$scheme_id_ok"
  echo "scheme_name_ok=$scheme_name_ok"
  echo "scheme_normative_ok=$scheme_normative_ok"
  echo "enforce_mode_ok=$enforce_mode_ok"
  echo "generated_scheme_id=$generated_scheme_id"
  echo "generated_scheme_name=$generated_scheme_name"
  echo "generated_scheme_normative=$generated_scheme_normative"
  echo "generated_enforce_mode=$generated_enforce_mode"
  echo "diff=$diff_file"
} >"$report"

{
  echo "backend_rawptr_contract_status=$status"
  echo "backend_rawptr_contract_baseline_sha256=$baseline_sha"
  echo "backend_rawptr_contract_generated_sha256=$generated_sha"
  echo "backend_rawptr_contract_closedloop_gate_ok=$closedloop_gate_ok"
  echo "backend_rawptr_contract_prod_closure_gate_ok=$prod_closure_gate_ok"
  echo "backend_rawptr_contract_scheme_id_ok=$scheme_id_ok"
  echo "backend_rawptr_contract_scheme_name_ok=$scheme_name_ok"
  echo "backend_rawptr_contract_scheme_normative_ok=$scheme_normative_ok"
  echo "backend_rawptr_contract_enforce_mode_ok=$enforce_mode_ok"
  echo "backend_rawptr_contract_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_rawptr_contract] rawptr contract baseline drift detected" 1>&2
  echo "  baseline: $baseline" 1>&2
  echo "  generated: $generated" 1>&2
  if [ -s "$diff_file" ]; then
    sed -n '1,120p' "$diff_file" 1>&2 || true
  fi
  if [ "$closedloop_missing" != "" ]; then
    echo "  missing in verify_backend_closedloop: $closedloop_missing" 1>&2
  fi
  if [ "$prod_closure_missing" != "" ]; then
    echo "  missing in backend_prod_closure: $prod_closure_missing" 1>&2
  fi
  if [ "$scheme_id_ok" != "1" ] || [ "$scheme_name_ok" != "1" ] || [ "$scheme_normative_ok" != "1" ] || [ "$enforce_mode_ok" != "1" ]; then
    echo "  reason: rawptr normative markers must be ZRPC + normative=1 + enforce.mode=hard_fail" 1>&2
  fi
  echo "  fix: ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} build_backend_rawptr_contract --doc:$doc --formal-doc:$formal_doc --tooling-doc:$tooling_doc --out:$baseline" 1>&2
  exit 1
fi

echo "verify_backend_rawptr_contract ok"
