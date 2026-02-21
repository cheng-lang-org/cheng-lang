#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_profile_schema.sh [--baseline:<path>]

Notes:
  - Verifies PAR-01 profile schema freeze file.
  - Regenerate with: sh src/tooling/build_backend_profile_schema.sh
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

baseline="src/tooling/backend_profile_schema.env"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --baseline:*)
      baseline="${1#--baseline:}"
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
  echo "[verify_backend_profile_schema] missing baseline file: $baseline" 1>&2
  exit 2
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_profile_schema] rg is required" 1>&2
  exit 2
fi

out_dir="artifacts/backend_profile"
mkdir -p "$out_dir"

generated="$out_dir/backend_profile_schema.generated.env"
report="$out_dir/backend_profile_schema.report.txt"
snapshot="$out_dir/backend_profile_schema.snapshot.env"
diff_file="$out_dir/backend_profile_schema.diff.txt"

sh src/tooling/build_backend_profile_schema.sh --out:"$generated" >/dev/null

status="ok"
if ! cmp -s "$baseline" "$generated"; then
  status="drift"
  if ! diff -u "$baseline" "$generated" >"$diff_file" 2>/dev/null; then
    diff "$baseline" "$generated" >"$diff_file" 2>/dev/null || true
  fi
else
  : >"$diff_file"
fi

# Freeze rule: closedloop must wire profile + plugin core steps.
closedloop_steps_ok="1"
if ! rg -q 'run_step "backend.profile_smoke"' src/tooling/verify_backend_closedloop.sh; then
  closedloop_steps_ok="0"
fi
for required_step in \
  backend.mem_contract \
  backend.mem_image_core \
  backend.mem_exe_emit \
  backend.linkerless_dev \
  backend.hotpatch_meta \
  backend.hotpatch_inplace \
  backend.incr_patch_fastpath \
  backend.mem_patch_regression \
  backend.plugin_isolation \
  backend.noptr_exemption_scope; do
  if ! rg -q "run_step \\\"$required_step\\\"" src/tooling/verify_backend_closedloop.sh; then
    closedloop_steps_ok="0"
  fi
done
if [ "$closedloop_steps_ok" != "1" ]; then
  status="drift"
fi

baseline_sha="$(hash_file "$baseline")"
generated_sha="$(hash_file "$generated")"

{
  echo "verify_backend_profile_schema report"
  echo "status=$status"
  echo "baseline=$baseline"
  echo "generated=$generated"
  echo "baseline_sha256=$baseline_sha"
  echo "generated_sha256=$generated_sha"
  echo "closedloop_steps_ok=$closedloop_steps_ok"
  echo "diff=$diff_file"
} >"$report"

{
  echo "backend_profile_schema_status=$status"
  echo "backend_profile_schema_baseline_sha256=$baseline_sha"
  echo "backend_profile_schema_generated_sha256=$generated_sha"
  echo "backend_profile_schema_closedloop_steps_ok=$closedloop_steps_ok"
  echo "backend_profile_schema_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_profile_schema] profile schema drift detected" 1>&2
  echo "  baseline: $baseline" 1>&2
  echo "  generated: $generated" 1>&2
  if [ -s "$diff_file" ]; then
    sed -n '1,80p' "$diff_file" 1>&2 || true
  fi
  echo "  fix: sh src/tooling/build_backend_profile_schema.sh --out:$baseline" 1>&2
  exit 1
fi

echo "verify_backend_profile_schema ok"
