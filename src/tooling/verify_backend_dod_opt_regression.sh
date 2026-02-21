#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_dod_opt_regression] rg is required" 1>&2
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

if [ "${BACKEND_DRIVER:-}" != "" ]; then
  driver="${BACKEND_DRIVER}"
else
  driver="$(env \
    BACKEND_DRIVER_PATH_PREFER_REBUILD="${BACKEND_DRIVER_PATH_PREFER_REBUILD:-1}" \
    BACKEND_DRIVER_ALLOW_FALLBACK="${BACKEND_DRIVER_ALLOW_FALLBACK:-0}" \
    sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_dod_opt_regression] backend driver not executable: $driver" 1>&2
  exit 1
fi

refresh_missing_marker="${PAR05_DRIVER_REFRESH:-0}"
case "$refresh_missing_marker" in
  0|1) ;;
  *) refresh_missing_marker="0" ;;
esac

refresh_driver_if_needed() {
  marker="$1"
  if driver_has_marker "$driver" "$marker"; then
    return 0
  fi
  if [ "$refresh_missing_marker" != "1" ]; then
    return 1
  fi
  refreshed_driver="$(env \
    BACKEND_DRIVER_PATH_PREFER_REBUILD=1 \
    BACKEND_DRIVER_ALLOW_FALLBACK=0 \
    sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
  if [ -x "$refreshed_driver" ] && driver_has_marker "$refreshed_driver" "$marker"; then
    echo "[verify_backend_dod_opt_regression] use refreshed backend driver: $refreshed_driver" 1>&2
    driver="$refreshed_driver"
    return 0
  fi
  return 1
}

if ! refresh_driver_if_needed "uir_opt2.noalias" && ! driver_has_marker "$driver" "uir_opt2.noalias"; then
  echo "[verify_backend_dod_opt_regression] backend driver missing noalias marker: $driver" 1>&2
  exit 1
fi
if ! refresh_driver_if_needed "uir_opt2.egraph" && ! driver_has_marker "$driver" "uir_opt2.egraph"; then
  echo "[verify_backend_dod_opt_regression] backend driver missing egraph marker: $driver" 1>&2
  exit 1
fi
if ! refresh_driver_if_needed "high_uir_checked_funcs" && ! driver_has_marker "$driver" "high_uir_checked_funcs"; then
  echo "[verify_backend_dod_opt_regression] backend driver missing phase generics marker: $driver" 1>&2
  exit 1
fi
if ! refresh_driver_if_needed "p4_phase_v1" && ! driver_has_marker "$driver" "p4_phase_v1"; then
  echo "[verify_backend_dod_opt_regression] backend driver missing phase contract marker: $driver" 1>&2
  exit 1
fi
if ! refresh_driver_if_needed "single_ir_dual_phase" && ! driver_has_marker "$driver" "single_ir_dual_phase"; then
  echo "[verify_backend_dod_opt_regression] backend driver missing phase model marker: $driver" 1>&2
  exit 1
fi

is_uint() {
  case "$1" in
    ''|*[!0-9]*) return 1 ;;
  esac
  return 0
}

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
guard_fixture="${BACKEND_DOD_OPT_GUARD_FIXTURE:-tests/cheng/backend/fixtures/return_store_deref.cheng}"
det_fixture="${BACKEND_DOD_OPT_DET_FIXTURE:-tests/cheng/backend/fixtures/return_opt2_inline_dce.cheng}"
benefit_fixture="${BACKEND_DOD_OPT_BENEFIT_FIXTURE:-tests/cheng/backend/fixtures/return_opt2_sroa_deref.cheng}"
iters="${UIR_EGRAPH_ITERS:-3}"
goal_raw="${UIR_EGRAPH_GOAL:-balanced}"
baseline_file="${BACKEND_DOD_OPT_BASELINE:-src/tooling/backend_dod_opt_regression_baseline.env}"

if [ ! -f "$guard_fixture" ]; then
  echo "[verify_backend_dod_opt_regression] missing guard fixture: $guard_fixture" 1>&2
  exit 2
fi
if [ ! -f "$det_fixture" ]; then
  echo "[verify_backend_dod_opt_regression] missing deterministic fixture: $det_fixture" 1>&2
  exit 2
fi
if [ ! -f "$benefit_fixture" ]; then
  echo "[verify_backend_dod_opt_regression] missing benefit fixture: $benefit_fixture" 1>&2
  exit 2
fi
if ! is_uint "$iters" || [ "$iters" -le 0 ] || [ "$iters" -gt 32 ]; then
  echo "[verify_backend_dod_opt_regression] invalid UIR_EGRAPH_ITERS: $iters (expected 1..32)" 1>&2
  exit 2
fi
if [ ! -f "$baseline_file" ]; then
  echo "[verify_backend_dod_opt_regression] missing baseline file: $baseline_file" 1>&2
  exit 2
fi

normalize_goal() {
  g="$1"
  lower="$(printf '%s' "$g" | tr 'A-Z' 'a-z')"
  case "$lower" in
    balanced|latency|size) printf '%s\n' "$lower" ;;
    *) printf 'balanced\n' ;;
  esac
}
goal="$(normalize_goal "$goal_raw")"

# shellcheck disable=SC1090
. "$baseline_file"

min_benefit_obj_size_delta="${BACKEND_DOD_OPT_MIN_BENEFIT_OBJ_SIZE_DELTA:-${BACKEND_DOD_OPT_MIN_BENEFIT_OBJ_SIZE_DELTA:-1}}"
min_benefit_noalias_total_changes="${BACKEND_DOD_OPT_MIN_BENEFIT_NOALIAS_TOTAL_CHANGES:-${BACKEND_DOD_OPT_MIN_BENEFIT_NOALIAS_TOTAL_CHANGES:-1}}"
min_benefit_noalias_changed_funcs="${BACKEND_DOD_OPT_MIN_BENEFIT_NOALIAS_CHANGED_FUNCS:-${BACKEND_DOD_OPT_MIN_BENEFIT_NOALIAS_CHANGED_FUNCS:-1}}"
drift_alert_min_ratio_pct="${BACKEND_DOD_OPT_DRIFT_ALERT_MIN_RATIO_PCT:-${BACKEND_DOD_OPT_DRIFT_ALERT_MIN_RATIO_PCT:-80}}"
drift_alert_strict="${BACKEND_DOD_OPT_DRIFT_ALERT_STRICT:-${BACKEND_DOD_OPT_DRIFT_ALERT_STRICT:-0}}"
baseline_benefit_obj_size_delta="${BACKEND_DOD_OPT_BASELINE_BENEFIT_OBJ_SIZE_DELTA:-${BACKEND_DOD_OPT_BASELINE_BENEFIT_OBJ_SIZE_DELTA:-0}}"
baseline_benefit_noalias_total_changes="${BACKEND_DOD_OPT_BASELINE_BENEFIT_NOALIAS_TOTAL_CHANGES:-${BACKEND_DOD_OPT_BASELINE_BENEFIT_NOALIAS_TOTAL_CHANGES:-0}}"
baseline_benefit_noalias_changed_funcs="${BACKEND_DOD_OPT_BASELINE_BENEFIT_NOALIAS_CHANGED_FUNCS:-${BACKEND_DOD_OPT_BASELINE_BENEFIT_NOALIAS_CHANGED_FUNCS:-0}}"

for numeric_pair in \
  "min_benefit_obj_size_delta:$min_benefit_obj_size_delta" \
  "min_benefit_noalias_total_changes:$min_benefit_noalias_total_changes" \
  "min_benefit_noalias_changed_funcs:$min_benefit_noalias_changed_funcs" \
  "drift_alert_min_ratio_pct:$drift_alert_min_ratio_pct" \
  "baseline_benefit_obj_size_delta:$baseline_benefit_obj_size_delta" \
  "baseline_benefit_noalias_total_changes:$baseline_benefit_noalias_total_changes" \
  "baseline_benefit_noalias_changed_funcs:$baseline_benefit_noalias_changed_funcs"; do
  metric_name="${numeric_pair%%:*}"
  metric_value="${numeric_pair#*:}"
  if ! is_uint "$metric_value"; then
    echo "[verify_backend_dod_opt_regression] invalid numeric setting ${metric_name}: ${metric_value}" 1>&2
    exit 2
  fi
done
if [ "$min_benefit_obj_size_delta" -le 0 ] || [ "$min_benefit_noalias_total_changes" -le 0 ] || [ "$min_benefit_noalias_changed_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] minimal benefit thresholds must be > 0" 1>&2
  exit 2
fi
if [ "$drift_alert_min_ratio_pct" -le 0 ] || [ "$drift_alert_min_ratio_pct" -gt 100 ]; then
  echo "[verify_backend_dod_opt_regression] drift alert min ratio pct out of range: $drift_alert_min_ratio_pct (expected 1..100)" 1>&2
  exit 2
fi
case "$drift_alert_strict" in
  0|1) ;;
  *)
    echo "[verify_backend_dod_opt_regression] invalid drift alert strict switch: $drift_alert_strict (expected 0|1)" 1>&2
    exit 2
    ;;
esac

for required in \
  "src/backend/uir/uir_noalias_pass.cheng" \
  "src/backend/uir/uir_egraph_rewrite.cheng" \
  "src/backend/uir/uir_egraph_cost.cheng" \
  "src/backend/uir/uir_opt.cheng" \
  "src/backend/tooling/backend_driver.cheng"; do
  if [ ! -f "$required" ]; then
    echo "[verify_backend_dod_opt_regression] missing file: $required" 1>&2
    exit 2
  fi
done

require_marker() {
  file="$1"
  pat="$2"
  label="$3"
  if ! rg -q "$pat" "$file"; then
    echo "[verify_backend_dod_opt_regression] missing marker ($label) in $file" 1>&2
    exit 1
  fi
}

require_marker "src/backend/uir/uir_noalias_pass.cheng" "fn uirRunNoAliasPrep\\(" "noalias_pass_entry"
require_marker "src/backend/uir/uir_noalias_pass.cheng" "proof_backed_changes=" "noalias_proof_backed_report_surface"
require_marker "src/backend/uir/uir_egraph_rewrite.cheng" "fn uirRunEGraphRewrite\\(" "egraph_rewrite_entry"
require_marker "src/backend/uir/uir_egraph_cost.cheng" "fn uirEGraphScore\\(" "egraph_cost_score"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.noalias" "noalias_profile_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.noalias\\.proof_required" "noalias_proof_required_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.egraph\\.proof_required" "egraph_proof_required_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.egraph" "egraph_profile_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.cost_model" "cost_model_profile_surface"
require_marker "src/backend/tooling/backend_driver.cheng" "high_uir_checked_funcs=" "driver_generics_phase_checked_surface"
require_marker "src/backend/tooling/backend_driver.cheng" "low_uir_lowered_funcs=" "driver_generics_phase_low_surface"
require_marker "src/backend/tooling/backend_driver.cheng" "high_uir_fallback_funcs=" "driver_generics_phase_fallback_surface"
require_marker "src/backend/tooling/backend_driver.cheng" "phase_contract_version=p4_phase_v1" "driver_generics_phase_contract_surface"
require_marker "src/backend/tooling/backend_driver.cheng" "uir_phase_model=single_ir_dual_phase" "driver_stamp_phase_model_surface"

out_dir="artifacts/backend_dod_opt_regression"
mkdir -p "$out_dir"
guard_obj_off="$out_dir/guard.noalias_off.o"
guard_obj_on="$out_dir/guard.noalias_on.o"
guard_log_off="$out_dir/guard.noalias_off.log"
guard_log_on="$out_dir/guard.noalias_on.log"
det_obj_a="$out_dir/det.noalias_on.a.o"
det_obj_b="$out_dir/det.noalias_on.b.o"
det_log_a="$out_dir/det.noalias_on.a.log"
det_log_b="$out_dir/det.noalias_on.b.log"
benefit_obj_off="$out_dir/benefit.noalias_off.o"
benefit_obj_on="$out_dir/benefit.noalias_on.o"
benefit_log_off="$out_dir/benefit.noalias_off.log"
benefit_log_on="$out_dir/benefit.noalias_on.log"
phase_default_obj="$out_dir/phase.default.noalias_on.o"
phase_default_log="$out_dir/phase.default.noalias_on.log"
phase_default_stamp="$out_dir/phase.default.noalias_on.compile_stamp.txt"
phase_on_obj="$out_dir/phase.ownership_on.noalias_on.o"
phase_on_log="$out_dir/phase.ownership_on.noalias_on.log"
phase_on_stamp="$out_dir/phase.ownership_on.noalias_on.compile_stamp.txt"
report="$out_dir/backend_dod_opt_regression.report.txt"
snapshot="$out_dir/backend_dod_opt_regression.snapshot.env"
rm -f \
  "$guard_obj_off" "$guard_obj_on" "$guard_log_off" "$guard_log_on" \
  "$det_obj_a" "$det_obj_b" "$det_log_a" "$det_log_b" \
  "$benefit_obj_off" "$benefit_obj_on" "$benefit_log_off" "$benefit_log_on" \
  "$phase_default_obj" "$phase_default_log" "$phase_default_stamp" \
  "$phase_on_obj" "$phase_on_log" "$phase_on_stamp"

build_one() {
  fixture="$1"
  noalias="$2"
  out_obj="$3"
  out_log="$4"
  set +e
  env \
    ABI=v2_noptr \
    STAGE1_STD_NO_POINTERS=0 \
    STAGE1_STD_NO_POINTERS_STRICT=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    UIR_PROFILE=1 \
    UIR_NOALIAS="$noalias" \
    UIR_EGRAPH_ITERS="$iters" \
    UIR_EGRAPH_GOAL="$goal" \
    BACKEND_OPT_LEVEL=2 \
    BACKEND_EMIT=obj \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$out_obj" \
    "$driver" >"$out_log" 2>&1
  rc="$?"
  set -e
  return "$rc"
}

build_phase_probe() {
  fixture="$1"
  out_obj="$2"
  out_log="$3"
  out_stamp="$4"
  ownership_mode="$5"
  rm -f "$out_obj" "$out_log" "$out_stamp"
  set +e
  if [ "$ownership_mode" = "default" ]; then
    env \
      ABI=v2_noptr \
      STAGE1_STD_NO_POINTERS=0 \
      STAGE1_STD_NO_POINTERS_STRICT=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      UIR_PROFILE=1 \
      STAGE1_SKIP_OWNERSHIP= \
      UIR_NOALIAS=1 \
      UIR_NOALIAS_REQUIRE_PROOF=1 \
      UIR_EGRAPH_ITERS="$iters" \
      UIR_EGRAPH_GOAL="$goal" \
      UIR_EGRAPH_REQUIRE_PROOF=1 \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      BACKEND_OPT_LEVEL=2 \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1
  elif [ "$ownership_mode" = "forced_on" ]; then
    env \
      ABI=v2_noptr \
      STAGE1_STD_NO_POINTERS=0 \
      STAGE1_STD_NO_POINTERS_STRICT=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      UIR_PROFILE=1 \
      STAGE1_SKIP_OWNERSHIP=0 \
      UIR_NOALIAS=1 \
      UIR_NOALIAS_REQUIRE_PROOF=1 \
      UIR_EGRAPH_ITERS="$iters" \
      UIR_EGRAPH_GOAL="$goal" \
      UIR_EGRAPH_REQUIRE_PROOF=1 \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      BACKEND_OPT_LEVEL=2 \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1
  else
    echo "[verify_backend_dod_opt_regression] invalid phase probe ownership mode: $ownership_mode" 1>&2
    set -e
    return 2
  fi
  rc="$?"
  set -e
  return "$rc"
}

profile_metric() {
  file="$1"
  event="$2"
  field="$3"
  awk -v target_event="$event" -v target_field="$field" '
    $1 == "uir_profile" && $2 == target_event {
      for (i = 3; i <= NF; i++) {
        split($i, kv, "=")
        if (kv[1] == target_field) {
          print kv[2]
          found = 1
          exit
        }
      }
    }
    END {
      if (!found) print ""
    }
  ' "$file"
}

profile_label_int_value() {
  file="$1"
  label_prefix="$2"
  awk -v target_prefix="$label_prefix" '
    $1 == "uir_profile" && index($2, target_prefix) == 1 {
      value = $2
      sub("^" target_prefix, "", value)
      print value
      found = 1
      exit
    }
    END {
      if (!found) print ""
    }
  ' "$file"
}

extract_tab_metric() {
  line="$1"
  key="$2"
  printf '%s\n' "$line" | tr '\t' '\n' | awk -F= -v target_key="$key" '$1==target_key{print $2; exit}'
}

extract_stamp_field() {
  file="$1"
  key="$2"
  awk -F= -v target_key="$key" '
    $1 == target_key {
      print substr($0, index($0, "=") + 1)
      found = 1
      exit
    }
    END {
      if (!found) print ""
    }
  ' "$file"
}

require_stamp_field_exact() {
  file="$1"
  key="$2"
  expected="$3"
  got="$(extract_stamp_field "$file" "$key")"
  if [ "$got" != "$expected" ]; then
    echo "[verify_backend_dod_opt_regression] compile stamp mismatch ($file): $key=$got expected=$expected" 1>&2
    exit 1
  fi
}

if ! build_one "$guard_fixture" 0 "$guard_obj_off" "$guard_log_off"; then
  echo "[verify_backend_dod_opt_regression] compile failed (guard/noalias=0): $guard_fixture" 1>&2
  tail -n 200 "$guard_log_off" 1>&2 || true
  exit 1
fi
if ! build_one "$guard_fixture" 1 "$guard_obj_on" "$guard_log_on"; then
  echo "[verify_backend_dod_opt_regression] compile failed (guard/noalias=1): $guard_fixture" 1>&2
  tail -n 200 "$guard_log_on" 1>&2 || true
  exit 1
fi
if [ ! -s "$guard_obj_off" ] || [ ! -s "$guard_obj_on" ]; then
  echo "[verify_backend_dod_opt_regression] missing guard outputs" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias\.disabled([[:space:]]+|$)' "$guard_log_off"; then
  echo "[verify_backend_dod_opt_regression] missing noalias disabled event: $guard_log_off" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias([[:space:]]+|$)' "$guard_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing noalias event: $guard_log_on" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.safe\.copy_prop([[:space:]]+|$)' "$guard_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing copy_prop event: $guard_log_on" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph([[:space:]]+|$)' "$guard_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing egraph event: $guard_log_on" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph\.changed([[:space:]]+|$)' "$guard_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing egraph changed event: $guard_log_on" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.cost_model([[:space:]]+|$)' "$guard_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing cost_model event: $guard_log_on" 1>&2
  exit 1
fi
if ! rg -q "^uir_profile[[:space:]]+uir_opt2\\.cost_model\\.${goal}([[:space:]]+|$)" "$guard_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing goal cost_model event (${goal}): $guard_log_on" 1>&2
  exit 1
fi
if ! cmp -s "$guard_obj_off" "$guard_obj_on"; then
  echo "[verify_backend_dod_opt_regression] guard object mismatch (expected fallback-stable): $guard_obj_off vs $guard_obj_on" 1>&2
  exit 1
fi

if ! build_one "$benefit_fixture" 0 "$benefit_obj_off" "$benefit_log_off"; then
  echo "[verify_backend_dod_opt_regression] compile failed (benefit/noalias=0): $benefit_fixture" 1>&2
  tail -n 200 "$benefit_log_off" 1>&2 || true
  exit 1
fi
if ! build_one "$benefit_fixture" 1 "$benefit_obj_on" "$benefit_log_on"; then
  echo "[verify_backend_dod_opt_regression] compile failed (benefit/noalias=1): $benefit_fixture" 1>&2
  tail -n 200 "$benefit_log_on" 1>&2 || true
  exit 1
fi
if [ ! -s "$benefit_obj_off" ] || [ ! -s "$benefit_obj_on" ]; then
  echo "[verify_backend_dod_opt_regression] missing benefit outputs" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias\.disabled([[:space:]]+|$)' "$benefit_log_off"; then
  echo "[verify_backend_dod_opt_regression] missing noalias disabled event: $benefit_log_off" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias([[:space:]]+|$)' "$benefit_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing noalias event: $benefit_log_on" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph([[:space:]]+|$)' "$benefit_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing egraph event: $benefit_log_on" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph\.changed([[:space:]]+|$)' "$benefit_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing egraph changed event: $benefit_log_on" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.cost_model([[:space:]]+|$)' "$benefit_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing cost_model event: $benefit_log_on" 1>&2
  exit 1
fi
if ! rg -q "^uir_profile[[:space:]]+uir_opt2\\.cost_model\\.${goal}([[:space:]]+|$)" "$benefit_log_on"; then
  echo "[verify_backend_dod_opt_regression] missing goal cost_model event (${goal}): $benefit_log_on" 1>&2
  exit 1
fi
benefit_noalias_report_line="$(rg '^noalias_report[[:space:]]+' "$benefit_log_on" | tail -n 1 || true)"
if [ "$benefit_noalias_report_line" = "" ]; then
  echo "[verify_backend_dod_opt_regression] missing noalias_report line: $benefit_log_on" 1>&2
  exit 1
fi

benefit_noalias_forwarded_loads="$(printf '%s\n' "$benefit_noalias_report_line" | tr '\t' '\n' | awk -F= '$1=="forward_loads"{print $2; exit}')"
benefit_noalias_mem2reg_loads="$(printf '%s\n' "$benefit_noalias_report_line" | tr '\t' '\n' | awk -F= '$1=="mem2reg_loads"{print $2; exit}')"
benefit_noalias_mem2reg_stores="$(printf '%s\n' "$benefit_noalias_report_line" | tr '\t' '\n' | awk -F= '$1=="mem2reg_stores"{print $2; exit}')"
benefit_noalias_changed_funcs="$(printf '%s\n' "$benefit_noalias_report_line" | tr '\t' '\n' | awk -F= '$1=="changed_funcs"{print $2; exit}')"

for metric_pair in \
  "forward_loads:$benefit_noalias_forwarded_loads" \
  "mem2reg_loads:$benefit_noalias_mem2reg_loads" \
  "mem2reg_stores:$benefit_noalias_mem2reg_stores" \
  "changed_funcs:$benefit_noalias_changed_funcs"; do
  metric_name="${metric_pair%%:*}"
  metric_value="${metric_pair#*:}"
  if ! is_uint "$metric_value"; then
    echo "[verify_backend_dod_opt_regression] invalid noalias_report metric ${metric_name}: ${metric_value}" 1>&2
    exit 1
  fi
done

benefit_noalias_total_changes=$((benefit_noalias_forwarded_loads + benefit_noalias_mem2reg_loads + benefit_noalias_mem2reg_stores))
if [ "$benefit_noalias_total_changes" -le 0 ] || [ "$benefit_noalias_changed_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] no effective noalias benefit signal: $benefit_noalias_report_line" 1>&2
  exit 1
fi

if ! build_one "$det_fixture" 1 "$det_obj_a" "$det_log_a"; then
  echo "[verify_backend_dod_opt_regression] compile failed (determinism/a): $det_fixture" 1>&2
  tail -n 200 "$det_log_a" 1>&2 || true
  exit 1
fi
if ! build_one "$det_fixture" 1 "$det_obj_b" "$det_log_b"; then
  echo "[verify_backend_dod_opt_regression] compile failed (determinism/b): $det_fixture" 1>&2
  tail -n 200 "$det_log_b" 1>&2 || true
  exit 1
fi
if [ ! -s "$det_obj_a" ] || [ ! -s "$det_obj_b" ]; then
  echo "[verify_backend_dod_opt_regression] missing deterministic outputs" 1>&2
  exit 1
fi
if ! cmp -s "$det_obj_a" "$det_obj_b"; then
  echo "[verify_backend_dod_opt_regression] deterministic object mismatch: $det_obj_a vs $det_obj_b" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias([[:space:]]+|$)' "$det_log_a"; then
  echo "[verify_backend_dod_opt_regression] missing noalias event: $det_log_a" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph([[:space:]]+|$)' "$det_log_a"; then
  echo "[verify_backend_dod_opt_regression] missing egraph event: $det_log_a" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph\.changed([[:space:]]+|$)' "$det_log_a"; then
  echo "[verify_backend_dod_opt_regression] missing egraph changed event: $det_log_a" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.cost_model([[:space:]]+|$)' "$det_log_a"; then
  echo "[verify_backend_dod_opt_regression] missing cost_model event: $det_log_a" 1>&2
  exit 1
fi
if ! rg -q "^uir_profile[[:space:]]+uir_opt2\\.cost_model\\.${goal}([[:space:]]+|$)" "$det_log_a"; then
  echo "[verify_backend_dod_opt_regression] missing goal cost_model event (${goal}): $det_log_a" 1>&2
  exit 1
fi

if ! build_phase_probe "$benefit_fixture" "$phase_default_obj" "$phase_default_log" "$phase_default_stamp" "default"; then
  echo "[verify_backend_dod_opt_regression] compile failed (phase/default ownership policy): $benefit_fixture" 1>&2
  tail -n 200 "$phase_default_log" 1>&2 || true
  exit 1
fi
if ! build_phase_probe "$benefit_fixture" "$phase_on_obj" "$phase_on_log" "$phase_on_stamp" "forced_on"; then
  echo "[verify_backend_dod_opt_regression] compile failed (phase/ownership-on): $benefit_fixture" 1>&2
  tail -n 200 "$phase_on_log" 1>&2 || true
  exit 1
fi
for required in \
  "$phase_default_obj" "$phase_on_obj" \
  "$phase_default_stamp" "$phase_on_stamp"; do
  if [ ! -s "$required" ]; then
    echo "[verify_backend_dod_opt_regression] missing phase artifact: $required" 1>&2
    exit 1
  fi
done

phase_default_generics_line="$(rg '^generics_report[[:space:]]+' "$phase_default_log" | tail -n 1 || true)"
phase_on_generics_line="$(rg '^generics_report[[:space:]]+' "$phase_on_log" | tail -n 1 || true)"
if [ "$phase_default_generics_line" = "" ]; then
  echo "[verify_backend_dod_opt_regression] missing generics_report in phase default log: $phase_default_log" 1>&2
  exit 1
fi
if [ "$phase_on_generics_line" = "" ]; then
  echo "[verify_backend_dod_opt_regression] missing generics_report in phase ownership-on log: $phase_on_log" 1>&2
  exit 1
fi

phase_default_high_uir_checked_funcs="$(extract_tab_metric "$phase_default_generics_line" "high_uir_checked_funcs")"
phase_default_low_uir_lowered_funcs="$(extract_tab_metric "$phase_default_generics_line" "low_uir_lowered_funcs")"
phase_default_high_uir_fallback_funcs="$(extract_tab_metric "$phase_default_generics_line" "high_uir_fallback_funcs")"
phase_default_contract_version="$(extract_tab_metric "$phase_default_generics_line" "phase_contract_version")"
phase_on_high_uir_checked_funcs="$(extract_tab_metric "$phase_on_generics_line" "high_uir_checked_funcs")"
phase_on_low_uir_lowered_funcs="$(extract_tab_metric "$phase_on_generics_line" "low_uir_lowered_funcs")"
phase_on_high_uir_fallback_funcs="$(extract_tab_metric "$phase_on_generics_line" "high_uir_fallback_funcs")"
phase_on_contract_version="$(extract_tab_metric "$phase_on_generics_line" "phase_contract_version")"

for metric_pair in \
  "phase_default_high_uir_checked_funcs:$phase_default_high_uir_checked_funcs" \
  "phase_default_low_uir_lowered_funcs:$phase_default_low_uir_lowered_funcs" \
  "phase_default_high_uir_fallback_funcs:$phase_default_high_uir_fallback_funcs" \
  "phase_on_high_uir_checked_funcs:$phase_on_high_uir_checked_funcs" \
  "phase_on_low_uir_lowered_funcs:$phase_on_low_uir_lowered_funcs" \
  "phase_on_high_uir_fallback_funcs:$phase_on_high_uir_fallback_funcs"; do
  metric_name="${metric_pair%%:*}"
  metric_value="${metric_pair#*:}"
  if ! is_uint "$metric_value"; then
    echo "[verify_backend_dod_opt_regression] invalid phase generics metric ${metric_name}: ${metric_value}" 1>&2
    exit 1
  fi
done

if [ "$phase_default_contract_version" != "p4_phase_v1" ]; then
  echo "[verify_backend_dod_opt_regression] phase contract version mismatch (default probe): $phase_default_contract_version" 1>&2
  exit 1
fi
if [ "$phase_on_contract_version" != "p4_phase_v1" ]; then
  echo "[verify_backend_dod_opt_regression] phase contract version mismatch (ownership-on probe): $phase_on_contract_version" 1>&2
  exit 1
fi
if [ "$phase_default_low_uir_lowered_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase default probe expected low_uir_lowered_funcs>0, got $phase_default_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ "$phase_on_low_uir_lowered_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase ownership-on probe expected low_uir_lowered_funcs>0, got $phase_on_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ $((phase_default_high_uir_checked_funcs + phase_default_high_uir_fallback_funcs)) -ne "$phase_default_low_uir_lowered_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase default totals mismatch: checked=$phase_default_high_uir_checked_funcs fallback=$phase_default_high_uir_fallback_funcs low=$phase_default_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ $((phase_on_high_uir_checked_funcs + phase_on_high_uir_fallback_funcs)) -ne "$phase_on_low_uir_lowered_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase ownership-on totals mismatch: checked=$phase_on_high_uir_checked_funcs fallback=$phase_on_high_uir_fallback_funcs low=$phase_on_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ "$phase_default_high_uir_checked_funcs" -ne 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase default probe expected high_uir_checked_funcs=0, got $phase_default_high_uir_checked_funcs" 1>&2
  exit 1
fi
if [ "$phase_default_high_uir_fallback_funcs" -ne "$phase_default_low_uir_lowered_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase default probe expected all lowered funcs fallback, fallback=$phase_default_high_uir_fallback_funcs low=$phase_default_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ "$phase_default_high_uir_fallback_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase default probe expected fallback funcs > 0" 1>&2
  exit 1
fi
if [ "$phase_on_high_uir_checked_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase ownership-on probe expected high_uir_checked_funcs>0, got $phase_on_high_uir_checked_funcs" 1>&2
  exit 1
fi

phase_default_noalias_report_line="$(rg '^noalias_report[[:space:]]+' "$phase_default_log" | tail -n 1 || true)"
phase_on_noalias_report_line="$(rg '^noalias_report[[:space:]]+' "$phase_on_log" | tail -n 1 || true)"
if [ "$phase_default_noalias_report_line" = "" ]; then
  echo "[verify_backend_dod_opt_regression] missing noalias_report in phase default log: $phase_default_log" 1>&2
  exit 1
fi
if [ "$phase_on_noalias_report_line" = "" ]; then
  echo "[verify_backend_dod_opt_regression] missing noalias_report in phase ownership-on log: $phase_on_log" 1>&2
  exit 1
fi

phase_default_noalias_proof_checked_funcs="$(extract_tab_metric "$phase_default_noalias_report_line" "proof_checked_funcs")"
phase_default_noalias_proof_skipped_funcs="$(extract_tab_metric "$phase_default_noalias_report_line" "proof_skipped_funcs")"
phase_default_noalias_proof_required="$(extract_tab_metric "$phase_default_noalias_report_line" "proof_required")"
phase_on_noalias_proof_checked_funcs="$(extract_tab_metric "$phase_on_noalias_report_line" "proof_checked_funcs")"
phase_on_noalias_proof_skipped_funcs="$(extract_tab_metric "$phase_on_noalias_report_line" "proof_skipped_funcs")"
phase_on_noalias_proof_required="$(extract_tab_metric "$phase_on_noalias_report_line" "proof_required")"

for metric_pair in \
  "phase_default_noalias_proof_checked_funcs:$phase_default_noalias_proof_checked_funcs" \
  "phase_default_noalias_proof_skipped_funcs:$phase_default_noalias_proof_skipped_funcs" \
  "phase_default_noalias_proof_required:$phase_default_noalias_proof_required" \
  "phase_on_noalias_proof_checked_funcs:$phase_on_noalias_proof_checked_funcs" \
  "phase_on_noalias_proof_skipped_funcs:$phase_on_noalias_proof_skipped_funcs" \
  "phase_on_noalias_proof_required:$phase_on_noalias_proof_required"; do
  metric_name="${metric_pair%%:*}"
  metric_value="${metric_pair#*:}"
  if ! is_uint "$metric_value"; then
    echo "[verify_backend_dod_opt_regression] invalid phase noalias proof metric ${metric_name}: ${metric_value}" 1>&2
    exit 1
  fi
done

if [ "$phase_default_noalias_proof_required" -ne 1 ] || [ "$phase_on_noalias_proof_required" -ne 1 ]; then
  echo "[verify_backend_dod_opt_regression] phase probes must enforce noalias proof-required mode" 1>&2
  exit 1
fi
if [ "$phase_default_noalias_proof_checked_funcs" -ne "$phase_default_high_uir_checked_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase default proof checked mismatch: noalias=$phase_default_noalias_proof_checked_funcs high_checked=$phase_default_high_uir_checked_funcs" 1>&2
  exit 1
fi
if [ "$phase_default_noalias_proof_skipped_funcs" -ne "$phase_default_high_uir_fallback_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase default proof skipped mismatch: noalias=$phase_default_noalias_proof_skipped_funcs fallback=$phase_default_high_uir_fallback_funcs" 1>&2
  exit 1
fi
if [ "$phase_default_noalias_proof_skipped_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase default probe expected proof_skipped_funcs>0" 1>&2
  exit 1
fi
if [ "$phase_on_noalias_proof_checked_funcs" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase ownership-on probe expected proof_checked_funcs>0" 1>&2
  exit 1
fi
if [ $((phase_on_noalias_proof_checked_funcs + phase_on_noalias_proof_skipped_funcs)) -ne "$phase_on_low_uir_lowered_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase ownership-on proof totals mismatch: checked=$phase_on_noalias_proof_checked_funcs skipped=$phase_on_noalias_proof_skipped_funcs low=$phase_on_low_uir_lowered_funcs" 1>&2
  exit 1
fi

if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias\.proof_required([[:space:]]+|$)' "$phase_default_log"; then
  echo "[verify_backend_dod_opt_regression] missing noalias proof-required profile event: $phase_default_log" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias\.proof_required([[:space:]]+|$)' "$phase_on_log"; then
  echo "[verify_backend_dod_opt_regression] missing noalias proof-required profile event: $phase_on_log" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph\.proof_required([[:space:]]+|$)' "$phase_default_log"; then
  echo "[verify_backend_dod_opt_regression] missing egraph proof-required profile event: $phase_default_log" 1>&2
  exit 1
fi
if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph\.proof_required([[:space:]]+|$)' "$phase_on_log"; then
  echo "[verify_backend_dod_opt_regression] missing egraph proof-required profile event: $phase_on_log" 1>&2
  exit 1
fi

phase_default_egraph_proof_backed_funcs="$(profile_label_int_value "$phase_default_log" "uir_opt2.egraph.proof_backed_funcs=")"
phase_on_egraph_proof_backed_funcs="$(profile_label_int_value "$phase_on_log" "uir_opt2.egraph.proof_backed_funcs=")"
if ! is_uint "$phase_default_egraph_proof_backed_funcs" || ! is_uint "$phase_on_egraph_proof_backed_funcs"; then
  echo "[verify_backend_dod_opt_regression] invalid egraph proof_backed_funcs profile metric in phase probes" 1>&2
  exit 1
fi
if [ "$phase_default_egraph_proof_backed_funcs" -ne "$phase_default_high_uir_checked_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase default egraph proof-backed mismatch: egraph=$phase_default_egraph_proof_backed_funcs high_checked=$phase_default_high_uir_checked_funcs" 1>&2
  exit 1
fi
if [ "$phase_on_egraph_proof_backed_funcs" -ne "$phase_on_high_uir_checked_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] phase ownership-on egraph proof-backed mismatch: egraph=$phase_on_egraph_proof_backed_funcs high_checked=$phase_on_high_uir_checked_funcs" 1>&2
  exit 1
fi

require_stamp_field_exact "$phase_default_stamp" "uir_phase_model" "single_ir_dual_phase"
require_stamp_field_exact "$phase_default_stamp" "uir_high_phase_contract" "ownership_func_v1"
require_stamp_field_exact "$phase_default_stamp" "uir_phase_contract_version" "p4_phase_v1"
require_stamp_field_exact "$phase_default_stamp" "stage1_skip_ownership_raw" ""
require_stamp_field_exact "$phase_default_stamp" "stage1_skip_ownership_effective" "1"
require_stamp_field_exact "$phase_default_stamp" "stage1_skip_ownership_default" "1"
require_stamp_field_exact "$phase_on_stamp" "uir_phase_model" "single_ir_dual_phase"
require_stamp_field_exact "$phase_on_stamp" "uir_high_phase_contract" "ownership_func_v1"
require_stamp_field_exact "$phase_on_stamp" "uir_phase_contract_version" "p4_phase_v1"
require_stamp_field_exact "$phase_on_stamp" "stage1_skip_ownership_raw" "0"
require_stamp_field_exact "$phase_on_stamp" "stage1_skip_ownership_effective" "0"
require_stamp_field_exact "$phase_on_stamp" "stage1_skip_ownership_default" "0"

phase_contract_ok="1"
phase_fallback_ok="1"
phase_contract_hint="dual_phase_contract_enforced"
phase_fallback_hint="default_policy_fallback_audited"

guard_size_off="$(wc -c < "$guard_obj_off" | tr -d ' ')"
guard_size_on="$(wc -c < "$guard_obj_on" | tr -d ' ')"
guard_size_delta="$((guard_size_off - guard_size_on))"
det_size_a="$(wc -c < "$det_obj_a" | tr -d ' ')"
det_size_b="$(wc -c < "$det_obj_b" | tr -d ' ')"
benefit_size_off="$(wc -c < "$benefit_obj_off" | tr -d ' ')"
benefit_size_on="$(wc -c < "$benefit_obj_on" | tr -d ' ')"
benefit_size_delta="$((benefit_size_off - benefit_size_on))"
if [ "$benefit_size_delta" -lt 0 ]; then
  echo "[verify_backend_dod_opt_regression] benefit fixture regressed object size: off=$benefit_size_off on=$benefit_size_on" 1>&2
  exit 1
fi
if [ "$benefit_size_delta" -lt "$min_benefit_obj_size_delta" ]; then
  echo "[verify_backend_dod_opt_regression] minimal benefit threshold failed (obj_size_delta): observed=$benefit_size_delta required>=$min_benefit_obj_size_delta" 1>&2
  exit 1
fi
if [ "$benefit_noalias_total_changes" -lt "$min_benefit_noalias_total_changes" ]; then
  echo "[verify_backend_dod_opt_regression] minimal benefit threshold failed (noalias_total_changes): observed=$benefit_noalias_total_changes required>=$min_benefit_noalias_total_changes" 1>&2
  exit 1
fi
if [ "$benefit_noalias_changed_funcs" -lt "$min_benefit_noalias_changed_funcs" ]; then
  echo "[verify_backend_dod_opt_regression] minimal benefit threshold failed (noalias_changed_funcs): observed=$benefit_noalias_changed_funcs required>=$min_benefit_noalias_changed_funcs" 1>&2
  exit 1
fi

ratio_pct_or_na() {
  observed="$1"
  baseline="$2"
  if ! is_uint "$observed" || ! is_uint "$baseline" || [ "$baseline" -le 0 ]; then
    echo "na"
    return 0
  fi
  echo $((observed * 100 / baseline))
}

drift_alert_count=0
drift_alert_status="ok"
drift_alert_details=""
append_drift_alert() {
  metric_name="$1"
  observed="$2"
  baseline="$3"
  ratio_pct="$4"
  drift_alert_count=$((drift_alert_count + 1))
  if [ "$drift_alert_details" = "" ]; then
    drift_alert_details="${metric_name}:observed=${observed},baseline=${baseline},ratio_pct=${ratio_pct}"
  else
    drift_alert_details="${drift_alert_details};${metric_name}:observed=${observed},baseline=${baseline},ratio_pct=${ratio_pct}"
  fi
}

check_drift_alert() {
  metric_name="$1"
  observed="$2"
  baseline="$3"
  if ! is_uint "$observed" || ! is_uint "$baseline" || [ "$baseline" -le 0 ]; then
    return 0
  fi
  ratio_pct=$((observed * 100 / baseline))
  if [ "$ratio_pct" -lt "$drift_alert_min_ratio_pct" ]; then
    append_drift_alert "$metric_name" "$observed" "$baseline" "$ratio_pct"
  fi
}

check_drift_alert "benefit_obj_size_delta" "$benefit_size_delta" "$baseline_benefit_obj_size_delta"
check_drift_alert "benefit_noalias_total_changes" "$benefit_noalias_total_changes" "$baseline_benefit_noalias_total_changes"
check_drift_alert "benefit_noalias_changed_funcs" "$benefit_noalias_changed_funcs" "$baseline_benefit_noalias_changed_funcs"

benefit_obj_size_ratio_pct="$(ratio_pct_or_na "$benefit_size_delta" "$baseline_benefit_obj_size_delta")"
benefit_noalias_total_changes_ratio_pct="$(ratio_pct_or_na "$benefit_noalias_total_changes" "$baseline_benefit_noalias_total_changes")"
benefit_noalias_changed_funcs_ratio_pct="$(ratio_pct_or_na "$benefit_noalias_changed_funcs" "$baseline_benefit_noalias_changed_funcs")"

if [ "$drift_alert_count" -gt 0 ]; then
  drift_alert_status="alert"
  echo "[verify_backend_dod_opt_regression] benefit drift alert: count=$drift_alert_count min_ratio_pct=$drift_alert_min_ratio_pct baseline=$baseline_file" 1>&2
  printf '%s\n' "$drift_alert_details" | tr ';' '\n' | sed '/^$/d; s/^/  - /' 1>&2
  if [ "$drift_alert_strict" = "1" ]; then
    echo "[verify_backend_dod_opt_regression] drift alert strict mode enabled; failing gate" 1>&2
    exit 1
  fi
fi

guard_total_ms_off="$(profile_metric "$guard_log_off" "single.emit_obj" "total_ms")"
guard_total_ms_on="$(profile_metric "$guard_log_on" "single.emit_obj" "total_ms")"
det_total_ms_a="$(profile_metric "$det_log_a" "single.emit_obj" "total_ms")"
det_total_ms_b="$(profile_metric "$det_log_b" "single.emit_obj" "total_ms")"
benefit_total_ms_off="$(profile_metric "$benefit_log_off" "single.emit_obj" "total_ms")"
benefit_total_ms_on="$(profile_metric "$benefit_log_on" "single.emit_obj" "total_ms")"
noalias_step_ms_on="$(profile_metric "$guard_log_on" "uir_opt2.noalias" "step_ms")"
egraph_step_ms_on="$(profile_metric "$guard_log_on" "uir_opt2.egraph" "step_ms")"
cost_model_step_ms_on="$(profile_metric "$guard_log_on" "uir_opt2.cost_model" "step_ms")"
noalias_disabled_hits="$(rg -c '^uir_profile[[:space:]]+uir_opt2\.noalias\.disabled([[:space:]]+|$)' "$guard_log_off" || true)"
noalias_enabled_hits="$(rg -c '^uir_profile[[:space:]]+uir_opt2\.noalias([[:space:]]+|$)' "$guard_log_on" || true)"
determinism_hint="deterministic_obj_equal"
fallback_hint="fallback_stable_guard_phase_audited"
benefit_hint="noalias_effective_size_stable"
if [ "$benefit_size_delta" -gt 0 ]; then
  benefit_hint="obj_size_reduced"
fi

{
  echo "verify_backend_dod_opt_regression report"
  echo "driver=$driver"
  echo "target=$target"
  echo "goal=$goal"
  echo "guard_fixture=$guard_fixture"
  echo "det_fixture=$det_fixture"
  echo "benefit_fixture=$benefit_fixture"
  echo "egraph_iters=$iters"
  echo "guard_obj_off=$guard_obj_off"
  echo "guard_obj_on=$guard_obj_on"
  echo "det_obj_a=$det_obj_a"
  echo "det_obj_b=$det_obj_b"
  echo "benefit_obj_off=$benefit_obj_off"
  echo "benefit_obj_on=$benefit_obj_on"
  echo "guard_log_off=$guard_log_off"
  echo "guard_log_on=$guard_log_on"
  echo "det_log_a=$det_log_a"
  echo "det_log_b=$det_log_b"
  echo "benefit_log_off=$benefit_log_off"
  echo "benefit_log_on=$benefit_log_on"
  echo "phase_default_log=$phase_default_log"
  echo "phase_on_log=$phase_on_log"
  echo "phase_default_stamp=$phase_default_stamp"
  echo "phase_on_stamp=$phase_on_stamp"
  echo "guard_total_ms_off=${guard_total_ms_off:-na}"
  echo "guard_total_ms_on=${guard_total_ms_on:-na}"
  echo "det_total_ms_a=${det_total_ms_a:-na}"
  echo "det_total_ms_b=${det_total_ms_b:-na}"
  echo "benefit_total_ms_off=${benefit_total_ms_off:-na}"
  echo "benefit_total_ms_on=${benefit_total_ms_on:-na}"
  echo "noalias_step_ms_on=${noalias_step_ms_on:-na}"
  echo "egraph_step_ms_on=${egraph_step_ms_on:-na}"
  echo "cost_model_step_ms_on=${cost_model_step_ms_on:-na}"
  echo "noalias_disabled_hits=${noalias_disabled_hits:-0}"
  echo "noalias_enabled_hits=${noalias_enabled_hits:-0}"
  echo "benefit_noalias_report=$benefit_noalias_report_line"
  echo "benefit_noalias_forwarded_loads=$benefit_noalias_forwarded_loads"
  echo "benefit_noalias_mem2reg_loads=$benefit_noalias_mem2reg_loads"
  echo "benefit_noalias_mem2reg_stores=$benefit_noalias_mem2reg_stores"
  echo "benefit_noalias_changed_funcs=$benefit_noalias_changed_funcs"
  echo "benefit_noalias_total_changes=$benefit_noalias_total_changes"
  echo "phase_default_generics_report=$phase_default_generics_line"
  echo "phase_on_generics_report=$phase_on_generics_line"
  echo "phase_default_noalias_report=$phase_default_noalias_report_line"
  echo "phase_on_noalias_report=$phase_on_noalias_report_line"
  echo "phase_default_high_uir_checked_funcs=$phase_default_high_uir_checked_funcs"
  echo "phase_default_low_uir_lowered_funcs=$phase_default_low_uir_lowered_funcs"
  echo "phase_default_high_uir_fallback_funcs=$phase_default_high_uir_fallback_funcs"
  echo "phase_on_high_uir_checked_funcs=$phase_on_high_uir_checked_funcs"
  echo "phase_on_low_uir_lowered_funcs=$phase_on_low_uir_lowered_funcs"
  echo "phase_on_high_uir_fallback_funcs=$phase_on_high_uir_fallback_funcs"
  echo "phase_default_noalias_proof_checked_funcs=$phase_default_noalias_proof_checked_funcs"
  echo "phase_default_noalias_proof_skipped_funcs=$phase_default_noalias_proof_skipped_funcs"
  echo "phase_default_noalias_proof_required=$phase_default_noalias_proof_required"
  echo "phase_on_noalias_proof_checked_funcs=$phase_on_noalias_proof_checked_funcs"
  echo "phase_on_noalias_proof_skipped_funcs=$phase_on_noalias_proof_skipped_funcs"
  echo "phase_on_noalias_proof_required=$phase_on_noalias_proof_required"
  echo "phase_default_egraph_proof_backed_funcs=$phase_default_egraph_proof_backed_funcs"
  echo "phase_on_egraph_proof_backed_funcs=$phase_on_egraph_proof_backed_funcs"
  echo "phase_contract_ok=$phase_contract_ok"
  echo "phase_fallback_ok=$phase_fallback_ok"
  echo "phase_contract_hint=$phase_contract_hint"
  echo "phase_fallback_hint=$phase_fallback_hint"
  echo "guard_obj_size_off=$guard_size_off"
  echo "guard_obj_size_on=$guard_size_on"
  echo "guard_obj_size_delta=$guard_size_delta"
  echo "det_obj_size_a=$det_size_a"
  echo "det_obj_size_b=$det_size_b"
  echo "benefit_obj_size_off=$benefit_size_off"
  echo "benefit_obj_size_on=$benefit_size_on"
  echo "benefit_obj_size_delta=$benefit_size_delta"
  echo "baseline_file=$baseline_file"
  echo "min_benefit_obj_size_delta=$min_benefit_obj_size_delta"
  echo "min_benefit_noalias_total_changes=$min_benefit_noalias_total_changes"
  echo "min_benefit_noalias_changed_funcs=$min_benefit_noalias_changed_funcs"
  echo "drift_alert_min_ratio_pct=$drift_alert_min_ratio_pct"
  echo "drift_alert_strict=$drift_alert_strict"
  echo "baseline_benefit_obj_size_delta=$baseline_benefit_obj_size_delta"
  echo "baseline_benefit_noalias_total_changes=$baseline_benefit_noalias_total_changes"
  echo "baseline_benefit_noalias_changed_funcs=$baseline_benefit_noalias_changed_funcs"
  echo "benefit_obj_size_ratio_pct=$benefit_obj_size_ratio_pct"
  echo "benefit_noalias_total_changes_ratio_pct=$benefit_noalias_total_changes_ratio_pct"
  echo "benefit_noalias_changed_funcs_ratio_pct=$benefit_noalias_changed_funcs_ratio_pct"
  echo "drift_alert_status=$drift_alert_status"
  echo "drift_alert_count=$drift_alert_count"
  echo "drift_alert_details=$drift_alert_details"
  echo "benefit_hint=$benefit_hint"
  echo "fallback_hint=$fallback_hint"
  echo "determinism_hint=$determinism_hint"
} >"$report"

{
  echo "backend_dod_opt_regression_driver=$driver"
  echo "backend_dod_opt_regression_target=$target"
  echo "backend_dod_opt_regression_goal=$goal"
  echo "backend_dod_opt_regression_guard_fixture=$guard_fixture"
  echo "backend_dod_opt_regression_det_fixture=$det_fixture"
  echo "backend_dod_opt_regression_benefit_fixture=$benefit_fixture"
  echo "backend_dod_opt_regression_iters=$iters"
  echo "backend_dod_opt_regression_report=$report"
  echo "backend_dod_opt_regression_guard_total_ms_off=${guard_total_ms_off:-}"
  echo "backend_dod_opt_regression_guard_total_ms_on=${guard_total_ms_on:-}"
  echo "backend_dod_opt_regression_det_total_ms_a=${det_total_ms_a:-}"
  echo "backend_dod_opt_regression_det_total_ms_b=${det_total_ms_b:-}"
  echo "backend_dod_opt_regression_benefit_total_ms_off=${benefit_total_ms_off:-}"
  echo "backend_dod_opt_regression_benefit_total_ms_on=${benefit_total_ms_on:-}"
  echo "backend_dod_opt_regression_phase_default_log=$phase_default_log"
  echo "backend_dod_opt_regression_phase_on_log=$phase_on_log"
  echo "backend_dod_opt_regression_phase_default_stamp=$phase_default_stamp"
  echo "backend_dod_opt_regression_phase_on_stamp=$phase_on_stamp"
  echo "backend_dod_opt_regression_noalias_step_ms_on=${noalias_step_ms_on:-}"
  echo "backend_dod_opt_regression_egraph_step_ms_on=${egraph_step_ms_on:-}"
  echo "backend_dod_opt_regression_cost_model_step_ms_on=${cost_model_step_ms_on:-}"
  echo "backend_dod_opt_regression_guard_obj_size_off=$guard_size_off"
  echo "backend_dod_opt_regression_guard_obj_size_on=$guard_size_on"
  echo "backend_dod_opt_regression_guard_obj_size_delta=$guard_size_delta"
  echo "backend_dod_opt_regression_det_obj_size_a=$det_size_a"
  echo "backend_dod_opt_regression_det_obj_size_b=$det_size_b"
  echo "backend_dod_opt_regression_benefit_obj_size_off=$benefit_size_off"
  echo "backend_dod_opt_regression_benefit_obj_size_on=$benefit_size_on"
  echo "backend_dod_opt_regression_benefit_obj_size_delta=$benefit_size_delta"
  echo "backend_dod_opt_regression_baseline_file=$baseline_file"
  echo "backend_dod_opt_regression_min_benefit_obj_size_delta=$min_benefit_obj_size_delta"
  echo "backend_dod_opt_regression_min_benefit_noalias_total_changes=$min_benefit_noalias_total_changes"
  echo "backend_dod_opt_regression_min_benefit_noalias_changed_funcs=$min_benefit_noalias_changed_funcs"
  echo "backend_dod_opt_regression_drift_alert_min_ratio_pct=$drift_alert_min_ratio_pct"
  echo "backend_dod_opt_regression_drift_alert_strict=$drift_alert_strict"
  echo "backend_dod_opt_regression_baseline_benefit_obj_size_delta=$baseline_benefit_obj_size_delta"
  echo "backend_dod_opt_regression_baseline_benefit_noalias_total_changes=$baseline_benefit_noalias_total_changes"
  echo "backend_dod_opt_regression_baseline_benefit_noalias_changed_funcs=$baseline_benefit_noalias_changed_funcs"
  echo "backend_dod_opt_regression_benefit_obj_size_ratio_pct=$benefit_obj_size_ratio_pct"
  echo "backend_dod_opt_regression_benefit_noalias_total_changes_ratio_pct=$benefit_noalias_total_changes_ratio_pct"
  echo "backend_dod_opt_regression_benefit_noalias_changed_funcs_ratio_pct=$benefit_noalias_changed_funcs_ratio_pct"
  echo "backend_dod_opt_regression_drift_alert_status=$drift_alert_status"
  echo "backend_dod_opt_regression_drift_alert_count=$drift_alert_count"
  echo "backend_dod_opt_regression_drift_alert_details=$drift_alert_details"
  echo "backend_dod_opt_regression_benefit_noalias_forwarded_loads=$benefit_noalias_forwarded_loads"
  echo "backend_dod_opt_regression_benefit_noalias_mem2reg_loads=$benefit_noalias_mem2reg_loads"
  echo "backend_dod_opt_regression_benefit_noalias_mem2reg_stores=$benefit_noalias_mem2reg_stores"
  echo "backend_dod_opt_regression_benefit_noalias_changed_funcs=$benefit_noalias_changed_funcs"
  echo "backend_dod_opt_regression_benefit_noalias_total_changes=$benefit_noalias_total_changes"
  echo "backend_dod_opt_regression_phase_default_high_uir_checked_funcs=$phase_default_high_uir_checked_funcs"
  echo "backend_dod_opt_regression_phase_default_low_uir_lowered_funcs=$phase_default_low_uir_lowered_funcs"
  echo "backend_dod_opt_regression_phase_default_high_uir_fallback_funcs=$phase_default_high_uir_fallback_funcs"
  echo "backend_dod_opt_regression_phase_on_high_uir_checked_funcs=$phase_on_high_uir_checked_funcs"
  echo "backend_dod_opt_regression_phase_on_low_uir_lowered_funcs=$phase_on_low_uir_lowered_funcs"
  echo "backend_dod_opt_regression_phase_on_high_uir_fallback_funcs=$phase_on_high_uir_fallback_funcs"
  echo "backend_dod_opt_regression_phase_default_noalias_proof_checked_funcs=$phase_default_noalias_proof_checked_funcs"
  echo "backend_dod_opt_regression_phase_default_noalias_proof_skipped_funcs=$phase_default_noalias_proof_skipped_funcs"
  echo "backend_dod_opt_regression_phase_default_noalias_proof_required=$phase_default_noalias_proof_required"
  echo "backend_dod_opt_regression_phase_on_noalias_proof_checked_funcs=$phase_on_noalias_proof_checked_funcs"
  echo "backend_dod_opt_regression_phase_on_noalias_proof_skipped_funcs=$phase_on_noalias_proof_skipped_funcs"
  echo "backend_dod_opt_regression_phase_on_noalias_proof_required=$phase_on_noalias_proof_required"
  echo "backend_dod_opt_regression_phase_default_egraph_proof_backed_funcs=$phase_default_egraph_proof_backed_funcs"
  echo "backend_dod_opt_regression_phase_on_egraph_proof_backed_funcs=$phase_on_egraph_proof_backed_funcs"
  echo "backend_dod_opt_regression_phase_contract_ok=$phase_contract_ok"
  echo "backend_dod_opt_regression_phase_fallback_ok=$phase_fallback_ok"
  echo "backend_dod_opt_regression_phase_contract_hint=$phase_contract_hint"
  echo "backend_dod_opt_regression_phase_fallback_hint=$phase_fallback_hint"
  echo "backend_dod_opt_regression_noalias_disabled_hits=${noalias_disabled_hits:-0}"
  echo "backend_dod_opt_regression_noalias_enabled_hits=${noalias_enabled_hits:-0}"
  echo "backend_dod_opt_regression_benefit_hint=$benefit_hint"
  echo "backend_dod_opt_regression_fallback_hint=$fallback_hint"
  echo "backend_dod_opt_regression_determinism_hint=$determinism_hint"
} >"$snapshot"

echo "verify_backend_dod_opt_regression ok"
