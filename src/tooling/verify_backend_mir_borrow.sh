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

normalize_lower_trim() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
}

extract_report_field() {
  line="$1"
  key="$2"
  printf '%s\n' "$line" | tr '\t' '\n' | awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }'
}

is_uint() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

is_true_like() {
  case "$1" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
  esac
  return 1
}

require_stage_label_any() {
  log_file="$1"
  expected_labels="$2"
  gate_name="$3"
  matched="0"
  for expected_stage_label in $(printf '%s' "$expected_labels" | tr ';' ' '); do
    if grep -Fq "[backend] stage1: $expected_stage_label" "$log_file"; then
      matched="1"
      break
    fi
  done
  if [ "$matched" != "1" ]; then
    echo "[verify_backend_mir_borrow] missing stage labels for $gate_name: $expected_labels" 1>&2
    echo "  log: $log_file" 1>&2
    exit 1
  fi
}

require_stamp_field_exact() {
  stamp_file="$1"
  field_key="$2"
  expected_value="$3"
  if ! grep -Fxq "$field_key=$expected_value" "$stamp_file"; then
    echo "[verify_backend_mir_borrow] compile stamp mismatch: $field_key expected=$expected_value file=$stamp_file" 1>&2
    exit 1
  fi
}

driver_has_marker() {
  marker="$1"
  if ! command -v strings >/dev/null 2>&1; then
    return 1
  fi
  tmp_strings="$(mktemp "${TMPDIR:-/tmp}/cheng_driver_strings.XXXXXX" 2>/dev/null || true)"
  if [ "$tmp_strings" = "" ]; then
    return 1
  fi
  set +e
  strings "$driver" 2>/dev/null >"$tmp_strings"
  strings_status="$?"
  rg -q "$marker" "$tmp_strings"
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
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_mir_borrow] backend driver not executable: $driver" 1>&2
  exit 1
fi
if ! driver_has_marker "generics_report"; then
  echo "[verify_backend_mir_borrow] backend driver missing generics_report marker: $driver" 1>&2
  exit 1
fi
if ! driver_has_marker "high_uir_checked_funcs"; then
  echo "[verify_backend_mir_borrow] backend driver missing high_uir_checked_funcs marker: $driver" 1>&2
  exit 1
fi
if ! driver_has_marker "low_uir_lowered_funcs"; then
  echo "[verify_backend_mir_borrow] backend driver missing low_uir_lowered_funcs marker: $driver" 1>&2
  exit 1
fi
if ! driver_has_marker "high_uir_fallback_funcs"; then
  echo "[verify_backend_mir_borrow] backend driver missing high_uir_fallback_funcs marker: $driver" 1>&2
  exit 1
fi

# Keep MIR borrow gate focused on borrow/lowering policy checks, not closure no-pointer policy.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0
export STAGE1_NO_POINTERS_NON_C_ABI=0
export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo auto)}"
borrow_ir="$(normalize_lower_trim "${BORROW_IR:-mir}")"
generic_lowering="$(normalize_lower_trim "${GENERIC_LOWERING:-mir_hybrid}")"
generic_mode="$(normalize_lower_trim "${MIR_BORROW_GENERIC_MODE:-${GENERIC_MODE:-dict}}")"
generic_budget="${MIR_BORROW_GENERIC_SPEC_BUDGET:-${GENERIC_SPEC_BUDGET:-0}}"
skip_sem="${MIR_BORROW_SKIP_SEM:-0}"
skip_ownership="${MIR_BORROW_SKIP_OWNERSHIP:-1}"
borrow_fixture="${MIR_BORROW_FIXTURE:-tests/cheng/backend/fixtures/return_add.cheng}"
generic_fixture="${MIR_BORROW_GENERIC_FIXTURE:-tests/cheng/backend/fixtures/return_hashmaps_bracket.cheng}"
if [ "${MIR_BORROW_FIXTURE:-}" = "" ] && [ ! -f "$borrow_fixture" ]; then
  borrow_fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi

skip_sem_norm="0"
if is_true_like "$skip_sem"; then
  skip_sem_norm="1"
fi
skip_ownership_norm="0"
if is_true_like "$skip_ownership"; then
  skip_ownership_norm="1"
fi

if ! is_uint "$generic_budget"; then
  echo "[verify_backend_mir_borrow] invalid generic budget: $generic_budget" 1>&2
  exit 2
fi
if [ "$borrow_ir" != "mir" ] && [ "$borrow_ir" != "stage1" ]; then
  echo "[verify_backend_mir_borrow] invalid BORROW_IR: $borrow_ir (expected mir|stage1)" 1>&2
  exit 2
fi
if [ "$generic_lowering" != "mir_hybrid" ] && [ "$generic_lowering" != "mir_dict" ]; then
  echo "[verify_backend_mir_borrow] invalid GENERIC_LOWERING: $generic_lowering (expected mir_hybrid|mir_dict)" 1>&2
  exit 2
fi
if [ ! -f "$borrow_fixture" ]; then
  echo "[verify_backend_mir_borrow] missing borrow fixture: $borrow_fixture" 1>&2
  exit 2
fi
if [ ! -f "$generic_fixture" ]; then
  echo "[verify_backend_mir_borrow] missing generic fixture: $generic_fixture" 1>&2
  exit 2
fi

out_dir="artifacts/backend_mir_borrow"
mkdir -p "$out_dir"
borrow_obj="$out_dir/mir_borrow_check.o"
borrow_log="$out_dir/mir_borrow_check.log"
borrow_stamp="$out_dir/mir_borrow_check.compile_stamp.txt"
generic_obj="$out_dir/mir_borrow_generic_check.o"
generic_log="$out_dir/mir_borrow_generic_check.log"
generic_stamp="$out_dir/mir_borrow_generic_check.compile_stamp.txt"
ownership_on_obj="$out_dir/mir_borrow_ownership_on_check.o"
ownership_on_log="$out_dir/mir_borrow_ownership_on_check.log"
ownership_on_stamp="$out_dir/mir_borrow_ownership_on_check.compile_stamp.txt"
default_policy_obj="$out_dir/mir_borrow_default_policy_check.o"
default_policy_log="$out_dir/mir_borrow_default_policy_check.log"
default_policy_stamp="$out_dir/mir_borrow_default_policy_check.compile_stamp.txt"
snapshot_file="$out_dir/mir_borrow.snapshot.env"
report_file="$out_dir/mir_borrow.report.txt"

compile_fixture() {
  fixture="$1"
  out_obj="$2"
  out_log="$3"
  out_stamp="$4"
  ownership_mode="$5"
  rm -f "$out_obj" "$out_log" "$out_stamp"
  set +e
  if [ "$ownership_mode" = "default" ]; then
    env \
      UIR_PROFILE=1 \
      BACKEND_DEBUG_STAGE1_PIPE=1 \
      BORROW_IR="$borrow_ir" \
      GENERIC_LOWERING="$generic_lowering" \
      GENERIC_MODE="$generic_mode" \
      GENERIC_SPEC_BUDGET="$generic_budget" \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      STAGE1_SKIP_SEM="$skip_sem_norm" \
      STAGE1_SKIP_OWNERSHIP= \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1
  elif [ "$ownership_mode" = "forced_on" ]; then
    env \
      UIR_PROFILE=1 \
      BACKEND_DEBUG_STAGE1_PIPE=1 \
      BORROW_IR="$borrow_ir" \
      GENERIC_LOWERING="$generic_lowering" \
      GENERIC_MODE="$generic_mode" \
      GENERIC_SPEC_BUDGET="$generic_budget" \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      STAGE1_SKIP_SEM="$skip_sem_norm" \
      STAGE1_SKIP_OWNERSHIP=0 \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1
  else
    env \
      UIR_PROFILE=1 \
      BACKEND_DEBUG_STAGE1_PIPE=1 \
      BORROW_IR="$borrow_ir" \
      GENERIC_LOWERING="$generic_lowering" \
      GENERIC_MODE="$generic_mode" \
      GENERIC_SPEC_BUDGET="$generic_budget" \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      STAGE1_SKIP_SEM="$skip_sem_norm" \
      STAGE1_SKIP_OWNERSHIP="$skip_ownership_norm" \
      BACKEND_EMIT=obj \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1
  fi
  status="$?"
  set -e
  return "$status"
}

borrow_status="0"
generic_status="0"
ownership_on_status="0"
default_policy_status="0"
compile_fixture "$borrow_fixture" "$borrow_obj" "$borrow_log" "$borrow_stamp" "explicit" || borrow_status="$?"
compile_fixture "$generic_fixture" "$generic_obj" "$generic_log" "$generic_stamp" "explicit" || generic_status="$?"
compile_fixture "$generic_fixture" "$ownership_on_obj" "$ownership_on_log" "$ownership_on_stamp" "forced_on" || ownership_on_status="$?"
compile_fixture "$borrow_fixture" "$default_policy_obj" "$default_policy_log" "$default_policy_stamp" "default" || default_policy_status="$?"

if [ "$borrow_status" != "0" ] || [ ! -s "$borrow_obj" ]; then
  echo "[verify_backend_mir_borrow] compile failed: $borrow_fixture" 1>&2
  echo "  log: $borrow_log" 1>&2
  echo "  status: $borrow_status" 1>&2
  exit 1
fi
if [ "$generic_status" != "0" ] || [ ! -s "$generic_obj" ]; then
  echo "[verify_backend_mir_borrow] compile failed: $generic_fixture" 1>&2
  echo "  log: $generic_log" 1>&2
  echo "  status: $generic_status" 1>&2
  exit 1
fi
if [ "$ownership_on_status" != "0" ] || [ ! -s "$ownership_on_obj" ]; then
  echo "[verify_backend_mir_borrow] compile failed (ownership-enabled): $generic_fixture" 1>&2
  echo "  log: $ownership_on_log" 1>&2
  echo "  status: $ownership_on_status" 1>&2
  exit 1
fi
if [ "$default_policy_status" != "0" ] || [ ! -s "$default_policy_obj" ]; then
  echo "[verify_backend_mir_borrow] compile failed (default ownership policy): $borrow_fixture" 1>&2
  echo "  log: $default_policy_log" 1>&2
  echo "  status: $default_policy_status" 1>&2
  exit 1
fi

expected_stage_labels="ownership_start"
if [ "$borrow_ir" = "mir" ]; then
  expected_stage_labels="mir_borrow_start;ownership_start"
fi
if [ "$skip_ownership_norm" = "1" ]; then
  if [ "$borrow_ir" = "mir" ]; then
    expected_stage_labels="mir_borrow_skipped;ownership_skipped"
  else
    expected_stage_labels="ownership_skipped"
  fi
fi
require_stage_label_any "$borrow_log" "$expected_stage_labels" "explicit_skip_ownership"
require_stage_label_any "$ownership_on_log" "mir_borrow_start;ownership_start" "ownership_enabled"

default_policy_stage_labels="ownership_skipped"
if [ "$borrow_ir" = "mir" ]; then
  default_policy_stage_labels="mir_borrow_skipped;ownership_skipped"
fi
require_stage_label_any "$default_policy_log" "$default_policy_stage_labels" "default_skip_ownership"

if [ ! -s "$generic_stamp" ]; then
  echo "[verify_backend_mir_borrow] compile stamp missing: $generic_stamp" 1>&2
  exit 1
fi
if [ ! -s "$ownership_on_stamp" ]; then
  echo "[verify_backend_mir_borrow] compile stamp missing: $ownership_on_stamp" 1>&2
  exit 1
fi
if [ ! -s "$default_policy_stamp" ]; then
  echo "[verify_backend_mir_borrow] compile stamp missing: $default_policy_stamp" 1>&2
  exit 1
fi

require_stamp_field_exact "$generic_stamp" "borrow_ir" "$borrow_ir"
require_stamp_field_exact "$generic_stamp" "generic_lowering" "$generic_lowering"
require_stamp_field_exact "$ownership_on_stamp" "borrow_ir" "$borrow_ir"
require_stamp_field_exact "$ownership_on_stamp" "generic_lowering" "$generic_lowering"
require_stamp_field_exact "$default_policy_stamp" "borrow_ir" "$borrow_ir"
require_stamp_field_exact "$default_policy_stamp" "generic_lowering" "$generic_lowering"
require_stamp_field_exact "$generic_stamp" "uir_phase_model" "single_ir_dual_phase"
require_stamp_field_exact "$generic_stamp" "uir_high_phase_contract" "ownership_func_v1"
require_stamp_field_exact "$generic_stamp" "uir_phase_contract_version" "p4_phase_v1"
require_stamp_field_exact "$ownership_on_stamp" "uir_phase_model" "single_ir_dual_phase"
require_stamp_field_exact "$ownership_on_stamp" "uir_high_phase_contract" "ownership_func_v1"
require_stamp_field_exact "$ownership_on_stamp" "uir_phase_contract_version" "p4_phase_v1"
require_stamp_field_exact "$default_policy_stamp" "uir_phase_model" "single_ir_dual_phase"
require_stamp_field_exact "$default_policy_stamp" "uir_high_phase_contract" "ownership_func_v1"
require_stamp_field_exact "$default_policy_stamp" "uir_phase_contract_version" "p4_phase_v1"

compile_stamp_policy_mode="runtime_v2"
if grep -Fq "stage1_skip_sem_raw=" "$generic_stamp"; then
  require_stamp_field_exact "$generic_stamp" "stage1_skip_sem_raw" "$skip_sem_norm"
  require_stamp_field_exact "$generic_stamp" "stage1_skip_sem_effective" "$skip_sem_norm"
  require_stamp_field_exact "$generic_stamp" "stage1_skip_ownership_raw" "$skip_ownership_norm"
  require_stamp_field_exact "$generic_stamp" "stage1_skip_ownership_effective" "$skip_ownership_norm"
  require_stamp_field_exact "$generic_stamp" "stage1_skip_ownership_default" "0"

  require_stamp_field_exact "$ownership_on_stamp" "stage1_skip_sem_raw" "$skip_sem_norm"
  require_stamp_field_exact "$ownership_on_stamp" "stage1_skip_sem_effective" "$skip_sem_norm"
  require_stamp_field_exact "$ownership_on_stamp" "stage1_skip_ownership_raw" "0"
  require_stamp_field_exact "$ownership_on_stamp" "stage1_skip_ownership_effective" "0"
  require_stamp_field_exact "$ownership_on_stamp" "stage1_skip_ownership_default" "0"

  require_stamp_field_exact "$default_policy_stamp" "stage1_skip_sem_raw" "$skip_sem_norm"
  require_stamp_field_exact "$default_policy_stamp" "stage1_skip_sem_effective" "$skip_sem_norm"
  require_stamp_field_exact "$default_policy_stamp" "stage1_skip_ownership_raw" ""
  require_stamp_field_exact "$default_policy_stamp" "stage1_skip_ownership_effective" "1"
  require_stamp_field_exact "$default_policy_stamp" "stage1_skip_ownership_default" "1"
else
  compile_stamp_policy_mode="source_contract_fallback"
  if ! grep -Fq 'stage1_skip_sem_raw=' src/backend/tooling/backend_driver.cheng; then
    echo "[verify_backend_mir_borrow] backend_driver compile stamp missing stage1_skip_sem_raw source marker" 1>&2
    exit 1
  fi
  if ! grep -Fq 'stage1_skip_sem_effective=' src/backend/tooling/backend_driver.cheng; then
    echo "[verify_backend_mir_borrow] backend_driver compile stamp missing stage1_skip_sem_effective source marker" 1>&2
    exit 1
  fi
  if ! grep -Fq 'stage1_skip_ownership_raw=' src/backend/tooling/backend_driver.cheng; then
    echo "[verify_backend_mir_borrow] backend_driver compile stamp missing stage1_skip_ownership_raw source marker" 1>&2
    exit 1
  fi
  if ! grep -Fq 'stage1_skip_ownership_effective=' src/backend/tooling/backend_driver.cheng; then
    echo "[verify_backend_mir_borrow] backend_driver compile stamp missing stage1_skip_ownership_effective source marker" 1>&2
    exit 1
  fi
  if ! grep -Fq 'stage1_skip_ownership_default=' src/backend/tooling/backend_driver.cheng; then
    echo "[verify_backend_mir_borrow] backend_driver compile stamp missing stage1_skip_ownership_default source marker" 1>&2
    exit 1
  fi
fi

generics_line=""
report_mode="$generic_mode"
report_budget="$generic_budget"
report_borrow_ir="$borrow_ir"
report_generic_lowering="$generic_lowering"
report_instances_specialized="0"
report_instances_total="0"
report_high_uir_checked_funcs="0"
report_low_uir_lowered_funcs="0"
report_high_uir_fallback_funcs="0"
report_phase_contract_version=""
ownership_on_line=""
ownership_on_high_uir_checked_funcs="0"
ownership_on_low_uir_lowered_funcs="0"
ownership_on_high_uir_fallback_funcs="0"
policy_fields_status="present"
generics_line="$(grep 'generics_report' "$generic_log" | tail -n 1 || true)"
if [ "$generics_line" = "" ]; then
  echo "[verify_backend_mir_borrow] missing generics_report in log: $generic_log" 1>&2
  exit 1
fi
ownership_on_line="$(grep 'generics_report' "$ownership_on_log" | tail -n 1 || true)"
if [ "$ownership_on_line" = "" ]; then
  echo "[verify_backend_mir_borrow] missing generics_report in ownership-enabled log: $ownership_on_log" 1>&2
  exit 1
fi

report_mode="$(extract_report_field "$generics_line" "mode")"
report_budget="$(extract_report_field "$generics_line" "spec_budget")"
report_borrow_ir="$(extract_report_field "$generics_line" "borrow_ir")"
report_generic_lowering="$(extract_report_field "$generics_line" "generic_lowering")"
report_instances_specialized="$(extract_report_field "$generics_line" "instances_specialized")"
report_instances_total="$(extract_report_field "$generics_line" "instances_total")"
report_high_uir_checked_funcs="$(extract_report_field "$generics_line" "high_uir_checked_funcs")"
report_low_uir_lowered_funcs="$(extract_report_field "$generics_line" "low_uir_lowered_funcs")"
report_high_uir_fallback_funcs="$(extract_report_field "$generics_line" "high_uir_fallback_funcs")"
report_phase_contract_version="$(extract_report_field "$generics_line" "phase_contract_version")"
ownership_on_high_uir_checked_funcs="$(extract_report_field "$ownership_on_line" "high_uir_checked_funcs")"
ownership_on_low_uir_lowered_funcs="$(extract_report_field "$ownership_on_line" "low_uir_lowered_funcs")"
ownership_on_high_uir_fallback_funcs="$(extract_report_field "$ownership_on_line" "high_uir_fallback_funcs")"

if [ "$report_borrow_ir" = "" ] || [ "$report_generic_lowering" = "" ]; then
  policy_fields_status="missing"
  echo "[verify_backend_mir_borrow] generics_report missing borrow_ir/generic_lowering fields: $generic_log" 1>&2
  exit 1
fi
if [ "$report_high_uir_checked_funcs" = "" ] || [ "$report_low_uir_lowered_funcs" = "" ] || [ "$report_high_uir_fallback_funcs" = "" ] || [ "$report_phase_contract_version" = "" ]; then
  policy_fields_status="missing"
  echo "[verify_backend_mir_borrow] generics_report missing phase contract fields: $generic_log" 1>&2
  exit 1
fi
if [ "$report_borrow_ir" != "$borrow_ir" ]; then
  echo "[verify_backend_mir_borrow] borrow_ir mismatch: report=$report_borrow_ir expected=$borrow_ir" 1>&2
  exit 1
fi
if [ "$report_generic_lowering" != "$generic_lowering" ]; then
  echo "[verify_backend_mir_borrow] generic_lowering mismatch: report=$report_generic_lowering expected=$generic_lowering" 1>&2
  exit 1
fi
if ! is_uint "$report_instances_specialized"; then
  echo "[verify_backend_mir_borrow] invalid instances_specialized: $report_instances_specialized" 1>&2
  exit 1
fi
if ! is_uint "$report_instances_total"; then
  echo "[verify_backend_mir_borrow] invalid instances_total: $report_instances_total" 1>&2
  exit 1
fi
if ! is_uint "$report_high_uir_checked_funcs"; then
  echo "[verify_backend_mir_borrow] invalid high_uir_checked_funcs: $report_high_uir_checked_funcs" 1>&2
  exit 1
fi
if ! is_uint "$report_low_uir_lowered_funcs"; then
  echo "[verify_backend_mir_borrow] invalid low_uir_lowered_funcs: $report_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if ! is_uint "$report_high_uir_fallback_funcs"; then
  echo "[verify_backend_mir_borrow] invalid high_uir_fallback_funcs: $report_high_uir_fallback_funcs" 1>&2
  exit 1
fi
if ! is_uint "$ownership_on_high_uir_checked_funcs"; then
  echo "[verify_backend_mir_borrow] invalid ownership_on high_uir_checked_funcs: $ownership_on_high_uir_checked_funcs" 1>&2
  exit 1
fi
if ! is_uint "$ownership_on_low_uir_lowered_funcs"; then
  echo "[verify_backend_mir_borrow] invalid ownership_on low_uir_lowered_funcs: $ownership_on_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if ! is_uint "$ownership_on_high_uir_fallback_funcs"; then
  echo "[verify_backend_mir_borrow] invalid ownership_on high_uir_fallback_funcs: $ownership_on_high_uir_fallback_funcs" 1>&2
  exit 1
fi
if [ "$report_phase_contract_version" != "p4_phase_v1" ]; then
  echo "[verify_backend_mir_borrow] phase contract version mismatch: report=$report_phase_contract_version expected=p4_phase_v1" 1>&2
  exit 1
fi
if [ "$report_instances_specialized" -gt "$generic_budget" ]; then
  echo "[verify_backend_mir_borrow] specialization budget exceeded: specialized=$report_instances_specialized budget=$generic_budget" 1>&2
  exit 1
fi
if [ "$report_low_uir_lowered_funcs" -le 0 ]; then
  echo "[verify_backend_mir_borrow] low_uir_lowered_funcs must be > 0, got $report_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ $((report_high_uir_checked_funcs + report_high_uir_fallback_funcs)) -ne "$report_low_uir_lowered_funcs" ]; then
  echo "[verify_backend_mir_borrow] explicit phase totals mismatch: checked=$report_high_uir_checked_funcs fallback=$report_high_uir_fallback_funcs low=$report_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ "$report_high_uir_checked_funcs" -gt "$report_low_uir_lowered_funcs" ]; then
  echo "[verify_backend_mir_borrow] high_uir_checked_funcs exceeds low_uir_lowered_funcs: high=$report_high_uir_checked_funcs low=$report_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ "$skip_ownership_norm" = "1" ]; then
  if [ "$report_high_uir_checked_funcs" -ne 0 ]; then
    echo "[verify_backend_mir_borrow] expected high_uir_checked_funcs=0 when ownership is skipped, got $report_high_uir_checked_funcs" 1>&2
    exit 1
  fi
  if [ "$report_high_uir_fallback_funcs" -ne "$report_low_uir_lowered_funcs" ]; then
    echo "[verify_backend_mir_borrow] expected all low funcs to be fallback when ownership is skipped, fallback=$report_high_uir_fallback_funcs low=$report_low_uir_lowered_funcs" 1>&2
    exit 1
  fi
else
  if [ "$report_high_uir_checked_funcs" -le 0 ]; then
    echo "[verify_backend_mir_borrow] expected high_uir_checked_funcs>0 when ownership is enabled, got $report_high_uir_checked_funcs" 1>&2
    exit 1
  fi
fi

if [ "$ownership_on_low_uir_lowered_funcs" -le 0 ]; then
  echo "[verify_backend_mir_borrow] ownership-enabled low_uir_lowered_funcs must be > 0, got $ownership_on_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ $((ownership_on_high_uir_checked_funcs + ownership_on_high_uir_fallback_funcs)) -ne "$ownership_on_low_uir_lowered_funcs" ]; then
  echo "[verify_backend_mir_borrow] ownership-enabled phase totals mismatch: checked=$ownership_on_high_uir_checked_funcs fallback=$ownership_on_high_uir_fallback_funcs low=$ownership_on_low_uir_lowered_funcs" 1>&2
  exit 1
fi
if [ "$ownership_on_high_uir_checked_funcs" -le 0 ]; then
  echo "[verify_backend_mir_borrow] expected ownership-enabled probe to produce high_uir_checked_funcs>0, got $ownership_on_high_uir_checked_funcs" 1>&2
  exit 1
fi

snapshot_time="$(date -u +'%Y-%m-%dT%H:%M:%SZ' 2>/dev/null || echo "<unknown>")"
git_head="$(git -C "$root" rev-parse --verify HEAD 2>/dev/null || echo "<unknown>")"
git_dirty_files="$(git -C "$root" status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
if [ "$git_dirty_files" = "" ]; then
  git_dirty_files="0"
fi

{
  echo "snapshot_time_utc=$snapshot_time"
  echo "git_head=$git_head"
  echo "git_dirty_files=$git_dirty_files"
  echo "driver=$driver"
  echo "target=$target"
  echo "borrow_ir=$borrow_ir"
  echo "generic_lowering=$generic_lowering"
  echo "generic_mode=$generic_mode"
  echo "generic_budget=$generic_budget"
  echo "skip_sem=$skip_sem_norm"
  echo "skip_ownership=$skip_ownership_norm"
  echo "borrow_fixture=$borrow_fixture"
  echo "generic_fixture=$generic_fixture"
  echo "borrow_log=$borrow_log"
  echo "generic_log=$generic_log"
  echo "ownership_on_log=$ownership_on_log"
  echo "default_policy_log=$default_policy_log"
  echo "generic_stamp=$generic_stamp"
  echo "ownership_on_stamp=$ownership_on_stamp"
  echo "default_policy_stamp=$default_policy_stamp"
  echo "expected_stage_label_default_policy=$default_policy_stage_labels"
  echo "compile_stamp_status=present"
  echo "compile_stamp_policy_mode=$compile_stamp_policy_mode"
  echo "policy_fields_status=$policy_fields_status"
  echo "generics_mode_report=$report_mode"
  echo "generics_budget_report=$report_budget"
  echo "instances_total=$report_instances_total"
  echo "instances_specialized=$report_instances_specialized"
  echo "high_uir_checked_funcs=$report_high_uir_checked_funcs"
  echo "low_uir_lowered_funcs=$report_low_uir_lowered_funcs"
  echo "high_uir_fallback_funcs=$report_high_uir_fallback_funcs"
  echo "ownership_on_high_uir_checked_funcs=$ownership_on_high_uir_checked_funcs"
  echo "ownership_on_low_uir_lowered_funcs=$ownership_on_low_uir_lowered_funcs"
  echo "ownership_on_high_uir_fallback_funcs=$ownership_on_high_uir_fallback_funcs"
  echo "phase_contract_version=$report_phase_contract_version"
} > "$snapshot_file"

{
  echo "verify_backend_mir_borrow report"
  echo "driver=$driver"
  echo "target=$target"
  echo "borrow_ir=$borrow_ir"
  echo "generic_lowering=$generic_lowering"
  echo "generic_mode=$generic_mode"
  echo "generic_budget=$generic_budget"
  echo "skip_sem=$skip_sem_norm"
  echo "skip_ownership=$skip_ownership_norm"
  echo "borrow_fixture=$borrow_fixture"
  echo "generic_fixture=$generic_fixture"
  echo "expected_stage_label=$expected_stage_labels"
  echo "expected_stage_label_ownership_on=mir_borrow_start;ownership_start"
  echo "expected_stage_label_default_policy=$default_policy_stage_labels"
  echo "generics_report=$generics_line"
  echo "ownership_on_generics_report=$ownership_on_line"
  echo "instances_total=$report_instances_total"
  echo "instances_specialized=$report_instances_specialized"
  echo "high_uir_checked_funcs=$report_high_uir_checked_funcs"
  echo "low_uir_lowered_funcs=$report_low_uir_lowered_funcs"
  echo "high_uir_fallback_funcs=$report_high_uir_fallback_funcs"
  echo "ownership_on_high_uir_checked_funcs=$ownership_on_high_uir_checked_funcs"
  echo "ownership_on_low_uir_lowered_funcs=$ownership_on_low_uir_lowered_funcs"
  echo "ownership_on_high_uir_fallback_funcs=$ownership_on_high_uir_fallback_funcs"
  echo "phase_contract_version=$report_phase_contract_version"
  echo "compile_stamp=$generic_stamp"
  echo "ownership_on_compile_stamp=$ownership_on_stamp"
  echo "default_policy_compile_stamp=$default_policy_stamp"
  echo "compile_stamp_status=present"
  echo "compile_stamp_policy_mode=$compile_stamp_policy_mode"
  echo "policy_fields_status=$policy_fields_status"
} > "$report_file"

echo "verify_backend_mir_borrow ok"
