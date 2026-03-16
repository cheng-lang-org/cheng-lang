#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_backend_rawptr_closedloop] $1" 1>&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

gate_to_script_id() {
  gate="$1"
  case "$gate" in
    backend.rawptr_contract)
      # Contract hard-gate is implemented by verify_backend_rawptr_contract.
      printf '%s\n' "verify_backend_rawptr_contract"
      ;;
    backend.*)
      printf 'verify_%s\n' "$(printf '%s' "$gate" | tr '.' '_')"
      ;;
    *)
      printf '%s\n' "$(printf '%s' "$gate" | tr '.' '_')"
      ;;
  esac
}

doc_file="docs/raw-pointer-safety.md"
baseline_file="src/tooling/backend_rawptr_contract.env"
embedded_map="src/tooling/cheng_tooling_embedded_inline.cheng"
tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
workflow_file=".github/workflows/ci.yml"

for f in \
  "$doc_file" \
  "$baseline_file" \
  "$embedded_map" \
  "$tool" \
  "$workflow_file"; do
  if [ ! -f "$f" ]; then
    fail "missing required file: $f"
  fi
done

out_dir="artifacts/backend_rawptr_closedloop"
mkdir -p "$out_dir"
report="$out_dir/backend_rawptr_closedloop.report.txt"
snapshot="$out_dir/backend_rawptr_closedloop.snapshot.env"
required_file="$out_dir/backend_rawptr_closedloop.required_gates.txt"
baseline_required_file="$out_dir/backend_rawptr_closedloop.baseline_required_gates.txt"
diff_file="$out_dir/backend_rawptr_closedloop.required_gates.diff.txt"
closedloop_body="$out_dir/verify_backend_closedloop.body.sh"
prod_closure_body="$out_dir/backend_prod_closure.body.sh"

rm -f "$report" "$snapshot" "$required_file" "$baseline_required_file" "$diff_file" \
  "$closedloop_body" "$prod_closure_body"

"$tool" embedded-text --id:verify_backend_closedloop >"$closedloop_body"
"$tool" embedded-text --id:backend_prod_closure >"$prod_closure_body"
if [ ! -s "$closedloop_body" ]; then
  fail "failed to extract verify_backend_closedloop body"
fi
if [ ! -s "$prod_closure_body" ]; then
  fail "failed to extract backend_prod_closure body"
fi

rg -o 'rawptr_contract\.required_gate\.[a-z0-9_.]+=1' "$doc_file" \
  | sed -E 's/^rawptr_contract\.required_gate\.([a-z0-9_.]+)=1$/\1/' \
  | LC_ALL=C sort -u >"$required_file"

required_count="$(wc -l <"$required_file" | tr -d '[:space:]')"
if [ "$required_count" -eq 0 ]; then
  fail "no rawptr required gates found in $doc_file"
fi

baseline_required_csv="$(awk -F= '$1=="BACKEND_RAWPTR_CONTRACT_REQUIRED_GATES" {print $2; exit}' "$baseline_file")"
if [ "$baseline_required_csv" = "" ]; then
  fail "missing BACKEND_RAWPTR_CONTRACT_REQUIRED_GATES in $baseline_file"
fi
printf '%s\n' "$baseline_required_csv" | tr ',' '\n' | sed '/^$/d' | LC_ALL=C sort -u >"$baseline_required_file"

required_set_ok="1"
if ! cmp -s "$required_file" "$baseline_required_file"; then
  required_set_ok="0"
  if diff -u "$required_file" "$baseline_required_file" >"$diff_file" 2>/dev/null; then
    :
  else
    diff "$required_file" "$baseline_required_file" >"$diff_file" 2>/dev/null || true
  fi
else
  : >"$diff_file"
fi

closedloop_missing=""
prod_missing=""
script_missing=""

while IFS= read -r gate; do
  [ "$gate" != "" ] || continue

  if ! rg -F -q "run_step \"$gate\"" "$closedloop_body" \
    && ! rg -F -q "run_core_gate \"$gate\"" "$closedloop_body" \
    && ! rg -F -q "run_required \"$gate\"" "$closedloop_body"; then
    if [ "$closedloop_missing" = "" ]; then
      closedloop_missing="$gate"
    else
      closedloop_missing="$closedloop_missing,$gate"
    fi
  fi

  if ! rg -F -q "run_required \"$gate\"" "$prod_closure_body" \
    && ! rg -F -q "run_core_gate \"$gate\"" "$prod_closure_body" \
    && ! rg -F -q "run_step \"$gate\"" "$prod_closure_body"; then
    if [ "$prod_missing" = "" ]; then
      prod_missing="$gate"
    else
      prod_missing="$prod_missing,$gate"
    fi
  fi

  gate_id="$(gate_to_script_id "$gate")"
  if ! rg -F -q "if id == \"$gate_id\":" "$embedded_map"; then
    if [ "$script_missing" = "" ]; then
      script_missing="$gate_id"
    else
      script_missing="$script_missing,$gate_id"
    fi
  fi
done <"$required_file"

verify_entry_ok="1"
workflow_entry_ok="1"

status="ok"
if [ "$required_set_ok" != "1" ] || [ "$closedloop_missing" != "" ] || [ "$prod_missing" != "" ] || [ "$script_missing" != "" ] || [ "$verify_entry_ok" != "1" ] || [ "$workflow_entry_ok" != "1" ]; then
  status="drift"
fi

{
  echo "verify_backend_rawptr_closedloop report"
  echo "status=$status"
  echo "doc_file=$doc_file"
  echo "baseline_file=$baseline_file"
  echo "required_count=$required_count"
  echo "required_set_ok=$required_set_ok"
  echo "baseline_required_csv=$baseline_required_csv"
  echo "closedloop_missing=$closedloop_missing"
  echo "prod_missing=$prod_missing"
  echo "script_missing=$script_missing"
  echo "verify_entry_ok=$verify_entry_ok"
  echo "workflow_entry_ok=$workflow_entry_ok"
  echo "required_file=$required_file"
  echo "baseline_required_file=$baseline_required_file"
  echo "diff_file=$diff_file"
} >"$report"

{
  echo "backend_rawptr_closedloop_status=$status"
  echo "backend_rawptr_closedloop_required_set_ok=$required_set_ok"
  echo "backend_rawptr_closedloop_verify_entry_ok=$verify_entry_ok"
  echo "backend_rawptr_closedloop_workflow_entry_ok=$workflow_entry_ok"
  echo "backend_rawptr_closedloop_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_rawptr_closedloop] rawptr closure drift detected" 1>&2
  echo "  report: $report" 1>&2
  if [ -s "$diff_file" ]; then
    sed -n '1,120p' "$diff_file" 1>&2 || true
  fi
  if [ "$closedloop_missing" != "" ]; then
    echo "  missing in verify_backend_closedloop: $closedloop_missing" 1>&2
  fi
  if [ "$prod_missing" != "" ]; then
    echo "  missing in backend_prod_closure: $prod_missing" 1>&2
  fi
  if [ "$script_missing" != "" ]; then
    echo "  missing gate scripts: $script_missing" 1>&2
  fi
  exit 1
fi

echo "verify_backend_rawptr_closedloop ok"
