#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  echo "verify_backend_opt2_impl_surface options:"
  echo "  (no flags)"
  exit 0
fi
if [ "${1:-}" != "" ]; then
  echo "[verify_backend_opt2_impl_surface] unknown arg: $1" 1>&2
  exit 1
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_opt2_impl_surface] rg is required" >&2
  exit 2
fi

out_dir="artifacts/backend_opt2_impl_surface"
rm -rf "$out_dir"
mkdir -p "$out_dir"

opt2_file="src/backend/uir/uir_internal/uir_core_opt2.cheng"
ssu_file="src/backend/uir/uir_internal/uir_core_ssu.cheng"
noalias_file="src/backend/uir/uir_noalias_pass.cheng"
placeholder_hits="$out_dir/opt2.placeholder_hits.txt"
report="$out_dir/verify_backend_opt2_impl_surface.report.txt"
snapshot="$out_dir/verify_backend_opt2_impl_surface.snapshot.env"

status="ok"
rg -n -U -P '(?s)^fn\s+(uirCore(?:OptimizeModule|Func)[A-Za-z0-9_]+)\([^\n]*\):\s*bool\s*=\n\s*return false' \
  "$opt2_file" >"$placeholder_hits" || true

placeholder_count="0"
if [ -s "$placeholder_hits" ]; then
  placeholder_count="$(wc -l <"$placeholder_hits" | tr -d '[:space:]')"
  status="drift"
fi

ssu_default_ok="1"
if ! rg -q 'UIR_SSU", true' "$ssu_file"; then
  ssu_default_ok="0"
  status="drift"
fi

njvl_default_ok="1"
if ! rg -q 'UIR_NOALIAS_NJVL_LITE", true' "$noalias_file"; then
  njvl_default_ok="0"
  status="drift"
fi

noalias_fields_ok="1"
for key in unknown_slot_clobbers unknown_global_clobbers kill_events; do
  if ! rg -q "$key" "$noalias_file"; then
    noalias_fields_ok="0"
    status="drift"
  fi
done

{
  echo "verify_backend_opt2_impl_surface report"
  echo "status=$status"
  echo "placeholder_hits=$placeholder_hits"
  echo "placeholder_count=$placeholder_count"
  echo "ssu_default_ok=$ssu_default_ok"
  echo "njvl_default_ok=$njvl_default_ok"
  echo "noalias_fields_ok=$noalias_fields_ok"
} >"$report"

{
  echo "backend_opt2_impl_surface_status=$status"
  echo "backend_opt2_impl_surface_placeholder_count=$placeholder_count"
  echo "backend_opt2_impl_surface_ssu_default_ok=$ssu_default_ok"
  echo "backend_opt2_impl_surface_njvl_default_ok=$njvl_default_ok"
  echo "backend_opt2_impl_surface_noalias_fields_ok=$noalias_fields_ok"
  echo "backend_opt2_impl_surface_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_opt2_impl_surface] drift detected" >&2
  echo "  report: $report" >&2
  if [ -s "$placeholder_hits" ]; then
    echo "  placeholder functions (top):" >&2
    sed -n '1,80p' "$placeholder_hits" >&2 || true
  fi
  exit 1
fi

echo "verify_backend_opt2_impl_surface ok"
