#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_mem_contract.sh [--baseline:<path>] [--doc:<path>]

Notes:
  - Verifies PAR-01 Memory-Exe + Hotpatch contract baseline.
  - Regenerate baseline with: sh src/tooling/build_backend_mem_contract.sh
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

baseline="src/tooling/backend_mem_contract.env"
doc="docs/backend-mem-hotpatch-contract.md"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --baseline:*)
      baseline="${1#--baseline:}"
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

if [ ! -f "$baseline" ]; then
  echo "[verify_backend_mem_contract] missing baseline file: $baseline" 1>&2
  exit 2
fi
if [ ! -f "$doc" ]; then
  echo "[verify_backend_mem_contract] missing contract doc: $doc" 1>&2
  exit 2
fi
if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_mem_contract] rg is required" 1>&2
  exit 2
fi

out_dir="artifacts/backend_mem_contract"
mkdir -p "$out_dir"

generated="$out_dir/backend_mem_contract.generated.env"
report="$out_dir/backend_mem_contract.report.txt"
snapshot="$out_dir/backend_mem_contract.snapshot.env"
diff_file="$out_dir/backend_mem_contract.diff.txt"

sh src/tooling/build_backend_mem_contract.sh --doc:"$doc" --out:"$generated" >/dev/null

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
if ! rg -q 'run_step "backend.mem_contract"' src/tooling/verify_backend_closedloop.sh; then
  closedloop_gate_ok="0"
fi
if ! rg -q 'run_required "backend.mem_contract"' src/tooling/backend_prod_closure.sh; then
  prod_closure_gate_ok="0"
fi
if [ "$closedloop_gate_ok" != "1" ] || [ "$prod_closure_gate_ok" != "1" ]; then
  status="drift"
fi

baseline_sha="$(hash_file "$baseline")"
generated_sha="$(hash_file "$generated")"

{
  echo "verify_backend_mem_contract report"
  echo "status=$status"
  echo "doc=$doc"
  echo "baseline=$baseline"
  echo "generated=$generated"
  echo "baseline_sha256=$baseline_sha"
  echo "generated_sha256=$generated_sha"
  echo "closedloop_gate_ok=$closedloop_gate_ok"
  echo "prod_closure_gate_ok=$prod_closure_gate_ok"
  echo "diff=$diff_file"
} >"$report"

{
  echo "backend_mem_contract_status=$status"
  echo "backend_mem_contract_baseline_sha256=$baseline_sha"
  echo "backend_mem_contract_generated_sha256=$generated_sha"
  echo "backend_mem_contract_closedloop_gate_ok=$closedloop_gate_ok"
  echo "backend_mem_contract_prod_closure_gate_ok=$prod_closure_gate_ok"
  echo "backend_mem_contract_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_mem_contract] contract baseline drift detected" 1>&2
  echo "  baseline: $baseline" 1>&2
  echo "  generated: $generated" 1>&2
  if [ -s "$diff_file" ]; then
    sed -n '1,120p' "$diff_file" 1>&2 || true
  fi
  echo "  fix: sh src/tooling/build_backend_mem_contract.sh --doc:$doc --out:$baseline" 1>&2
  exit 1
fi

echo "verify_backend_mem_contract ok"
