#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/verify_backend_pure_cheng_surface.sh

PURE-01 gate for the active strict sidecar/proof surface.
Checks only the active strict manifest and forbids compat/fallback surface drift.
USAGE
}

root="${TOOLING_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)}"
cd "$root"

case "${1:-}" in
  --help|-h)
    usage
    exit 0
    ;;
  "")
    ;;
  *)
    echo "[verify_backend_pure_cheng_surface] unknown arg: $1" 1>&2
    usage 1>&2
    exit 2
    ;;
esac

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_pure_cheng_surface] rg is required" 1>&2
  exit 2
fi

out_dir="$root/artifacts/backend_pure_cheng_surface"
report="$out_dir/backend_pure_cheng_surface.report.txt"
snapshot="$out_dir/backend_pure_cheng_surface.snapshot.env"
active_surface_file="$out_dir/backend_pure_cheng_surface.active_surface.txt"
active_c_compat_file="$out_dir/backend_pure_cheng_surface.active_c_compat.txt"
active_fallback_file="$out_dir/backend_pure_cheng_surface.active_fallback.txt"
compat_pattern_file="$out_dir/backend_pure_cheng_surface.compat_patterns.txt"
fallback_pattern_file="$out_dir/backend_pure_cheng_surface.fallback_patterns.txt"
runtime_direct_recursion_file="$out_dir/backend_pure_cheng_surface.runtime_direct_recursion.txt"
enforce="${BACKEND_PURE_CHENG_ENFORCE:-1}"
status="ok"

rm -rf "$out_dir"
mkdir -p "$out_dir"

{
  printf '%s\n' "src/backend/tooling/backend_driver_proof.cheng"
  printf '%s\n' "src/backend/tooling/backend_driver_uir_shared.cheng"
  printf '%s\n' "src/backend/tooling/backend_driver_uir_module_shared.cheng"
  printf '%s\n' "src/backend/tooling/backend_driver_uir_sidecar_wrapper.cheng"
  printf '%s\n' "src/backend/tooling/backend_driver_uir_sidecar.cheng"
  printf '%s\n' "src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh"
  printf '%s\n' "src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh"
  printf '%s\n' "src/tooling/cheng_tooling_embedded_scripts/verify_backend_sidecar_cheng_fresh.sh"
  printf '%s\n' "src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh"
  printf '%s\n' "src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh"
} >"$active_surface_file"

printf '%s\n' \
  'backend_driver_c_sidecar_' \
  'driver_c_build_module_stage1(_direct)?' \
  'backend_driver_uir_sidecar_runtime_compat\.c' \
  >"$compat_pattern_file"

printf '%s\n' \
  'emergency_c' \
  'dist/releases/current/cheng' \
  >"$fallback_pattern_file"

: >"$active_c_compat_file"
: >"$active_fallback_file"
xargs rg -n -f "$compat_pattern_file" <"$active_surface_file" >"$active_c_compat_file" || true
xargs rg -n -f "$fallback_pattern_file" <"$active_surface_file" >"$active_fallback_file" || true
: >"$runtime_direct_recursion_file"

check_runtime_direct_recursion() {
  src="$1"
  awk '
    /driver_c_build_module_stage1_direct[[:space:]]*\(/ { in_fn=1 }
    in_fn { print }
    in_fn && /^}/ { exit }
  ' "$src" | rg -n 'cheng_sidecar_build_module_symbol' >"$runtime_direct_recursion_file.tmp" || true
  if [ -s "$runtime_direct_recursion_file.tmp" ]; then
    {
      echo "$src"
      cat "$runtime_direct_recursion_file.tmp"
    } >>"$runtime_direct_recursion_file"
  fi
  rm -f "$runtime_direct_recursion_file.tmp"
}

check_runtime_direct_recursion "$root/src/runtime/native/system_helpers.c"
check_runtime_direct_recursion "$root/src/runtime/native/system_helpers_selflink_min_runtime.c"
check_runtime_direct_recursion "$root/src/runtime/native/system_helpers_selflink_shim.c"

active_c_compat_count="$(wc -l <"$active_c_compat_file" | tr -d ' ')"
active_fallback_count="$(wc -l <"$active_fallback_file" | tr -d ' ')"
runtime_direct_recursion_count="$(wc -l <"$runtime_direct_recursion_file" | tr -d ' ')"

if [ "$active_c_compat_count" != "0" ] || [ "$active_fallback_count" != "0" ] || \
   [ "$runtime_direct_recursion_count" != "0" ]; then
  status="drift"
fi

{
  echo "verify_backend_pure_cheng_surface report"
  echo "contract_scope=active_strict_sidecar_proof_surface"
  echo "status=$status"
  echo "enforce=$enforce"
  echo "active_surface_file=$active_surface_file"
  echo "active_c_compat_count=$active_c_compat_count"
  echo "active_fallback_count=$active_fallback_count"
  echo "runtime_direct_recursion_count=$runtime_direct_recursion_count"
  echo "active_c_compat_file=$active_c_compat_file"
  echo "active_fallback_file=$active_fallback_file"
  echo "runtime_direct_recursion_file=$runtime_direct_recursion_file"
} >"$report"

{
  echo "backend_pure_cheng_surface_status=$status"
  echo "backend_pure_cheng_surface_enforce=$enforce"
  echo "backend_pure_cheng_surface_report=$report"
  echo "backend_pure_cheng_surface_active_c_compat_count=$active_c_compat_count"
  echo "backend_pure_cheng_surface_active_fallback_count=$active_fallback_count"
  echo "backend_pure_cheng_surface_runtime_direct_recursion_count=$runtime_direct_recursion_count"
} >"$snapshot"

if [ "$status" != "ok" ] && [ "$enforce" = "1" ]; then
  echo "[verify_backend_pure_cheng_surface] pure-Cheng surface drift detected" 1>&2
  echo "  report: $report" 1>&2
  sed -n '1,120p' "$active_c_compat_file" 1>&2 || true
  sed -n '1,120p' "$active_fallback_file" 1>&2 || true
  sed -n '1,120p' "$runtime_direct_recursion_file" 1>&2 || true
  exit 1
fi

echo "verify_backend_pure_cheng_surface ok"
