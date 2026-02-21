#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_profile_baseline.sh [--baseline:<path>] [--schema:<path>]

Notes:
  - Verifies PAR-01 baseline freeze file.
  - Regenerate with: sh src/tooling/build_backend_profile_baseline.sh
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

baseline="src/tooling/backend_profile_baseline.env"
schema="src/tooling/backend_profile_schema.env"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --baseline:*)
      baseline="${1#--baseline:}"
      ;;
    --schema:*)
      schema="${1#--schema:}"
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
  echo "[verify_backend_profile_baseline] missing baseline file: $baseline" 1>&2
  exit 2
fi
if [ ! -f "$schema" ]; then
  echo "[verify_backend_profile_baseline] missing schema file: $schema" 1>&2
  exit 2
fi
if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_profile_baseline] rg is required" 1>&2
  exit 2
fi

out_dir="artifacts/backend_profile"
mkdir -p "$out_dir"

generated="$out_dir/backend_profile_baseline.generated.env"
report="$out_dir/backend_profile_baseline.report.txt"
snapshot="$out_dir/backend_profile_baseline.snapshot.env"
diff_file="$out_dir/backend_profile_baseline.diff.txt"

sh src/tooling/build_backend_profile_baseline.sh --schema:"$schema" --out:"$generated" >/dev/null

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

# Freeze rule: production closure must wire profile schema/baseline gates.
closure_gate_ok="1"
if ! rg -q 'run_required "backend.profile_schema"' src/tooling/backend_prod_closure.sh; then
  closure_gate_ok="0"
fi
if ! rg -q 'run_required "backend.profile_baseline"' src/tooling/backend_prod_closure.sh; then
  closure_gate_ok="0"
fi
if [ "$closure_gate_ok" != "1" ]; then
  status="drift"
fi

baseline_sha="$(hash_file "$baseline")"
generated_sha="$(hash_file "$generated")"

{
  echo "verify_backend_profile_baseline report"
  echo "status=$status"
  echo "schema=$schema"
  echo "baseline=$baseline"
  echo "generated=$generated"
  echo "baseline_sha256=$baseline_sha"
  echo "generated_sha256=$generated_sha"
  echo "closure_gate_ok=$closure_gate_ok"
  echo "diff=$diff_file"
} >"$report"

{
  echo "backend_profile_baseline_status=$status"
  echo "backend_profile_baseline_baseline_sha256=$baseline_sha"
  echo "backend_profile_baseline_generated_sha256=$generated_sha"
  echo "backend_profile_baseline_closure_gate_ok=$closure_gate_ok"
  echo "backend_profile_baseline_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_profile_baseline] profile baseline drift detected" 1>&2
  echo "  baseline: $baseline" 1>&2
  echo "  generated: $generated" 1>&2
  if [ -s "$diff_file" ]; then
    sed -n '1,80p' "$diff_file" 1>&2 || true
  fi
  echo "  fix: sh src/tooling/build_backend_profile_baseline.sh --schema:$schema --out:$baseline" 1>&2
  exit 1
fi

echo "verify_backend_profile_baseline ok"
