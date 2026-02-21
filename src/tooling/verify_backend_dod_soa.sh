#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    rc=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$rc"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

if [ "${BACKEND_DRIVER:-}" != "" ]; then
  driver="${BACKEND_DRIVER}"
else
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_dod_soa] backend driver not executable: $driver" 1>&2
  exit 1
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_dod_soa] rg is required" 1>&2
  exit 2
fi

driver_has_marker() {
  cand="$1"
  marker="$2"
  if [ ! -x "$cand" ]; then
    return 1
  fi
  if ! command -v strings >/dev/null 2>&1; then
    return 0
  fi
  tmp_strings="$(mktemp "${TMPDIR:-/tmp}/cheng_driver_strings.XXXXXX" 2>/dev/null || true)"
  if [ "$tmp_strings" = "" ]; then
    return 1
  fi
  set +e
  strings "$cand" 2>/dev/null >"$tmp_strings"
  strings_status="$?"
  rg -F -q "$marker" "$tmp_strings"
  status="$?"
  set -e
  rm -f "$tmp_strings" 2>/dev/null || true
  if [ "$strings_status" -ne 0 ] && [ "$status" -ne 0 ]; then
    return 1
  fi
  [ "$status" -eq 0 ]
}

is_uint() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

parse_stage1_total_ms() {
  printf '%s\n' "$1" | sed -n 's/.* total=\([0-9][0-9]*\)ms.*/\1/p'
}

normalize_fixture_lines() {
  printf '%s\n' "$1" | tr ';' '\n' | tr '\r' '\n' | sed -e '/^[[:space:]]*$/d'
}

safe_name() {
  printf '%s' "$1" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_'
}

require_marker() {
  file="$1"
  pattern="$2"
  name="$3"
  if ! rg -q "$pattern" "$file"; then
    echo "[verify_backend_dod_soa] missing marker ($name) in $file" 1>&2
    exit 1
  fi
}

refresh_missing_marker="${PAR03_DRIVER_REFRESH:-0}"
case "$refresh_missing_marker" in
  0|1) ;;
  *) refresh_missing_marker="0" ;;
esac

if ! driver_has_marker "$driver" "uir_profile" || ! driver_has_marker "$driver" "generics_report"; then
  if [ "$refresh_missing_marker" = "1" ]; then
    refreshed_driver="$(env \
      BACKEND_DRIVER_PATH_PREFER_REBUILD=1 \
      BACKEND_DRIVER_ALLOW_FALLBACK=0 \
      sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
    refreshed_uir_profile="0"
    refreshed_generics_report="0"
    if [ -x "$refreshed_driver" ]; then
      if driver_has_marker "$refreshed_driver" "uir_profile"; then
        refreshed_uir_profile="1"
      fi
      if driver_has_marker "$refreshed_driver" "generics_report"; then
        refreshed_generics_report="1"
      fi
    fi
    if [ -x "$refreshed_driver" ] && [ "$refreshed_uir_profile" = "1" ] && [ "$refreshed_generics_report" = "1" ]; then
      echo "[verify_backend_dod_soa] use refreshed backend driver: $refreshed_driver" 1>&2
      driver="$refreshed_driver"
    fi
  fi
fi

if ! driver_has_marker "$driver" "uir_profile"; then
  echo "[verify_backend_dod_soa] backend driver missing uir_profile marker: $driver" 1>&2
  exit 1
fi
if ! driver_has_marker "$driver" "generics_report"; then
  echo "[verify_backend_dod_soa] backend driver missing generics_report marker: $driver" 1>&2
  exit 1
fi

baseline_file="${BACKEND_DOD_SOA_BASELINE:-src/tooling/backend_dod_soa_baseline.env}"
if [ ! -f "$baseline_file" ]; then
  echo "[verify_backend_dod_soa] missing baseline file: $baseline_file" 1>&2
  exit 2
fi

# shellcheck disable=SC1090
. "$baseline_file"

runs="${BACKEND_DOD_SOA_RUNS:-${BACKEND_DOD_SOA_RUNS:-3}}"
baseline_total_ms="${BACKEND_DOD_SOA_BASELINE_TOTAL_MS:-${BACKEND_DOD_SOA_BASELINE_TOTAL_MS:-240}}"
min_improvement_pct="${BACKEND_DOD_SOA_MIN_IMPROVEMENT_PCT:-${BACKEND_DOD_SOA_MIN_IMPROVEMENT_PCT:-25}}"
fixtures_raw="${BACKEND_DOD_SOA_FIXTURES:-${BACKEND_DOD_SOA_FIXTURES:-tests/cheng/backend/fixtures/return_object_fields.cheng;tests/cheng/backend/fixtures/return_hashmaps_bracket.cheng;tests/cheng/backend/fixtures/return_opt2_licm_while_cond.cheng}}"
target="${BACKEND_TARGET:-${BACKEND_DOD_SOA_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}}"

if ! is_uint "$runs" || [ "$runs" -le 0 ] || [ "$runs" -gt 12 ]; then
  echo "[verify_backend_dod_soa] invalid runs: $runs (expected 1..12)" 1>&2
  exit 2
fi
if ! is_uint "$baseline_total_ms" || [ "$baseline_total_ms" -le 0 ]; then
  echo "[verify_backend_dod_soa] invalid baseline total ms: $baseline_total_ms" 1>&2
  exit 2
fi
if ! is_uint "$min_improvement_pct" || [ "$min_improvement_pct" -ge 100 ]; then
  echo "[verify_backend_dod_soa] invalid min improvement pct: $min_improvement_pct" 1>&2
  exit 2
fi

required_max_ms=$((baseline_total_ms * (100 - min_improvement_pct) / 100))
if [ "$required_max_ms" -le 0 ]; then
  required_max_ms=1
fi

require_marker "src/stage1/ast.cheng" 'nodeArena: Node\[\]' 'stage1_node_arena'
require_marker "src/stage1/ast.cheng" 'nodeArenaEnabled: bool = true' 'stage1_node_arena_enabled'
require_marker "src/backend/uir/uir_internal/uir_core_types.cheng" 'localIndex: int32' 'uir_local_index_int32'
require_marker "src/backend/uir/uir_internal/uir_core_types.cheng" 'slot: int32' 'uir_slot_index_int32'
require_marker "src/backend/uir/uir_internal/uir_core_types.cheng" 'frameOff: int32' 'uir_frame_offset_int32'
require_marker "src/backend/uir/uir_internal/uir_core_ssa.cheng" 'rpoIndex: int32\[\]' 'uir_ssa_rpo_index_int32'

fixture_lines="$(normalize_fixture_lines "$fixtures_raw")"
if [ "$fixture_lines" = "" ]; then
  echo "[verify_backend_dod_soa] no fixtures configured" 1>&2
  exit 2
fi

out_dir="artifacts/backend_dod_soa"
mkdir -p "$out_dir"
report_file="$out_dir/backend_dod_soa.report.txt"
snapshot_file="$out_dir/backend_dod_soa.snapshot.env"
per_fixture_file="$out_dir/backend_dod_soa.per_fixture.tsv"
: >"$per_fixture_file"

overall_sum=0
overall_n=0

while IFS= read -r fixture; do
  [ "$fixture" = "" ] && continue
  if [ ! -f "$fixture" ]; then
    echo "[verify_backend_dod_soa] missing fixture: $fixture" 1>&2
    exit 2
  fi

  base="$(basename "$fixture" .cheng)"
  safe_base="$(safe_name "$base")"
  fixture_sum=0
  run_idx=1

  while [ "$run_idx" -le "$runs" ]; do
    log_path="$out_dir/${safe_base}.r${run_idx}.log"
    obj_path="$out_dir/${safe_base}.r${run_idx}.o"
    rm -f "$log_path" "$obj_path"

    set +e
    env \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_EMIT=obj \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$obj_path" \
      BACKEND_PROFILE=1 \
      UIR_PROFILE=1 \
      STAGE1_PROFILE=1 \
      STAGE1_STD_NO_POINTERS=0 \
      STAGE1_STD_NO_POINTERS_STRICT=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
      STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      "$driver" >"$log_path" 2>&1
    rc=$?
    set -e

    if [ "$rc" -ne 0 ]; then
      echo "[verify_backend_dod_soa] compile failed: fixture=$fixture run=$run_idx status=$rc" 1>&2
      sed -n '1,120p' "$log_path" 1>&2 || true
      exit 1
    fi
    if [ ! -s "$obj_path" ]; then
      echo "[verify_backend_dod_soa] missing obj output: $obj_path" 1>&2
      exit 1
    fi

    if ! rg -q '^backend_profile[[:space:]]+build_module[[:space:]]+' "$log_path"; then
      echo "[verify_backend_dod_soa] missing backend_profile build_module: $log_path" 1>&2
      exit 1
    fi
    if ! rg -q '^uir_profile[[:space:]]+build_module[[:space:]]+' "$log_path"; then
      echo "[verify_backend_dod_soa] missing uir_profile build_module: $log_path" 1>&2
      exit 1
    fi
    if ! rg -q '^generics_report[[:space:]]+ir=uir[[:space:]]+' "$log_path"; then
      echo "[verify_backend_dod_soa] missing generics_report: $log_path" 1>&2
      exit 1
    fi

    stage1_line="$(rg '^\[stage1\] profile:' "$log_path" | tail -n 1 || true)"
    if [ "$stage1_line" = "" ]; then
      echo "[verify_backend_dod_soa] missing stage1 profile line: $log_path" 1>&2
      exit 1
    fi
    total_ms="$(parse_stage1_total_ms "$stage1_line")"
    if ! is_uint "$total_ms"; then
      echo "[verify_backend_dod_soa] invalid stage1 total ms in profile: $stage1_line" 1>&2
      exit 1
    fi

    fixture_sum=$((fixture_sum + total_ms))
    overall_sum=$((overall_sum + total_ms))
    overall_n=$((overall_n + 1))
    run_idx=$((run_idx + 1))
  done

  fixture_avg_ms=$((fixture_sum / runs))
  printf '%s\t%s\t%s\n' "$fixture" "$fixture_avg_ms" "$runs" >>"$per_fixture_file"
done <<EOF
$fixture_lines
EOF

if [ "$overall_n" -le 0 ]; then
  echo "[verify_backend_dod_soa] no fixture runs executed" 1>&2
  exit 2
fi

observed_avg_ms=$((overall_sum / overall_n))
improvement_pct=$(( (baseline_total_ms - observed_avg_ms) * 100 / baseline_total_ms ))

if [ "$observed_avg_ms" -gt "$required_max_ms" ]; then
  echo "[verify_backend_dod_soa] performance regression: avg=${observed_avg_ms}ms required<=${required_max_ms}ms baseline=${baseline_total_ms}ms min_improvement=${min_improvement_pct}%" 1>&2
  exit 1
fi

{
  echo "verify_backend_dod_soa report"
  echo "driver=$driver"
  echo "target=$target"
  echo "baseline_file=$baseline_file"
  echo "runs=$runs"
  echo "baseline_total_ms=$baseline_total_ms"
  echo "min_improvement_pct=$min_improvement_pct"
  echo "required_max_ms=$required_max_ms"
  echo "observed_avg_ms=$observed_avg_ms"
  echo "improvement_pct=$improvement_pct"
  echo "per_fixture=$per_fixture_file"
} >"$report_file"

{
  echo "backend_dod_soa_target=$target"
  echo "backend_dod_soa_baseline_file=$baseline_file"
  echo "backend_dod_soa_runs=$runs"
  echo "backend_dod_soa_baseline_total_ms=$baseline_total_ms"
  echo "backend_dod_soa_required_max_ms=$required_max_ms"
  echo "backend_dod_soa_observed_avg_ms=$observed_avg_ms"
  echo "backend_dod_soa_improvement_pct=$improvement_pct"
  echo "backend_dod_soa_report=$report_file"
} >"$snapshot_file"

echo "verify_backend_dod_soa ok"
