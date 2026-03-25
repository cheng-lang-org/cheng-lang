#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"
. "$root/src/tooling/cheng_tooling_embedded_scripts/proof_phase_driver_common.sh"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  cat <<'EOF'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/verify_backend_mir_borrow.sh [--help]

Env:
  MIR_BORROW_IR=<mir|stage1>                default: mir
  MIR_BORROW_GENERIC_LOWERING=<mode>        default: mir_hybrid
  MIR_BORROW_GENERIC_BUDGET=<n>             default: 1
  MIR_BORROW_FIXTURE=<path>                 default: tests/cheng/compiler/fixtures/tooling_borrow_repro_no_call.cheng
  MIR_BORROW_GENERIC_FIXTURE=<path>         default: tests/cheng/backend/fixtures/return_new_expr_generic_box.cheng
  BACKEND_DRIVER=<path>                     optional explicit backend driver override

Notes:
  - Verifies MIR borrow / generic lowering behavior on the ownership-on proof surface.
  - Validates compile stamps plus proof-surface noalias/ssu runtime reports.
EOF
  exit 0
fi

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
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

extract_stamp_field() {
  stamp_file="$1"
  field_key="$2"
  awk -F= -v key="$field_key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$stamp_file"
}

require_ownership_fixed_0_field_exact() {
  stamp_file="$1"
  suffix="$2"
  expected_value="$3"
  new_key="stage1_ownership_fixed_0_${suffix}"
  old_key="stage1_skip_ownership_${suffix}"
  if grep -Fq "${new_key}=" "$stamp_file"; then
    require_stamp_field_exact "$stamp_file" "$new_key" "$expected_value"
    return
  fi
  require_stamp_field_exact "$stamp_file" "$old_key" "$expected_value"
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

require_stamp_field_exact() {
  stamp_file="$1"
  field_key="$2"
  expected_value="$3"
  if ! grep -Fxq "$field_key=$expected_value" "$stamp_file"; then
    echo "[verify_backend_mir_borrow] compile stamp mismatch: $field_key expected=$expected_value file=$stamp_file" 1>&2
    exit 1
  fi
}

require_stamp_field_falsey() {
  stamp_file="$1"
  field_key="$2"
  value="$(awk -F= -v key="$field_key" '$1 == key { print substr($0, index($0, "=") + 1); found=1; exit } END { if (!found) print "__missing__" }' "$stamp_file")"
  if [ "$value" = "__missing__" ]; then
    echo "[verify_backend_mir_borrow] compile stamp missing: $field_key file=$stamp_file" 1>&2
    exit 1
  fi
  case "$value" in
    ""|0|false|FALSE|no|NO)
      return 0
      ;;
  esac
  echo "[verify_backend_mir_borrow] compile stamp falsey mismatch: $field_key value=$value file=$stamp_file" 1>&2
  exit 1
}

require_log_pattern() {
  log_file="$1"
  pattern="$2"
  marker_name="$3"
  if ! rg -q "$pattern" "$log_file"; then
    echo "[verify_backend_mir_borrow] missing $marker_name: $log_file" 1>&2
    exit 1
  fi
}

parse_noalias_report() {
  log_file="$1"
  report_label="$2"
  line="$(rg '^noalias_report[[:space:]]+' "$log_file" | tail -n 1 || true)"
  if [ "$line" = "" ]; then
    echo "[verify_backend_mir_borrow] missing noalias_report ($report_label): $log_file" 1>&2
    exit 1
  fi
  proof_checked_funcs="$(extract_report_field "$line" "proof_checked_funcs")"
  proof_required="$(extract_report_field "$line" "proof_required")"
  unknown_slot_clobbers="$(extract_report_field "$line" "unknown_slot_clobbers")"
  unknown_global_clobbers="$(extract_report_field "$line" "unknown_global_clobbers")"
  for metric_pair in \
    "proof_checked_funcs:$proof_checked_funcs" \
    "proof_required:$proof_required" \
    "unknown_slot_clobbers:$unknown_slot_clobbers" \
    "unknown_global_clobbers:$unknown_global_clobbers"; do
    metric_name="${metric_pair%%:*}"
    metric_value="${metric_pair#*:}"
    if ! is_uint "$metric_value"; then
      echo "[verify_backend_mir_borrow] invalid noalias_report metric (${report_label}/${metric_name}): $metric_value" 1>&2
      exit 1
    fi
  done
  echo "$line|$proof_checked_funcs|$proof_required|$unknown_slot_clobbers|$unknown_global_clobbers"
}

parse_ssu_report() {
  log_file="$1"
  report_label="$2"
  line="$(rg '^ssu_report[[:space:]]+' "$log_file" | tail -n 1 || true)"
  if [ "$line" = "" ]; then
    echo "[verify_backend_mir_borrow] missing ssu_report ($report_label): $log_file" 1>&2
    exit 1
  fi
  enabled="$(extract_report_field "$line" "enabled")"
  tracked_funcs="$(extract_report_field "$line" "tracked_funcs")"
  dup_candidates="$(extract_report_field "$line" "dup_candidates")"
  move_candidates="$(extract_report_field "$line" "move_candidates")"
  use_version_max="$(extract_report_field "$line" "use_version_max")"
  for metric_pair in \
    "enabled:$enabled" \
    "tracked_funcs:$tracked_funcs" \
    "dup_candidates:$dup_candidates" \
    "move_candidates:$move_candidates" \
    "use_version_max:$use_version_max"; do
    metric_name="${metric_pair%%:*}"
    metric_value="${metric_pair#*:}"
    if ! is_uint "$metric_value"; then
      echo "[verify_backend_mir_borrow] invalid ssu_report metric (${report_label}/${metric_name}): $metric_value" 1>&2
      exit 1
    fi
  done
  echo "$line|$enabled|$tracked_funcs|$dup_candidates|$move_candidates|$use_version_max"
}

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
if [ "${BACKEND_DRIVER:-}" != "" ]; then
  driver="${BACKEND_DRIVER}"
else
  driver="$($tool driver-path --path-only 2>/dev/null | tail -n 1 | tr -d '\r' || true)"
  if [ "$driver" = "" ]; then
    driver="artifacts/backend_driver/cheng"
  fi
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_mir_borrow] backend driver not executable: $driver" 1>&2
  exit 1
fi

# Keep MIR borrow gate focused on borrow/lowering policy checks, not closure no-pointer policy.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0
export STAGE1_NO_POINTERS_NON_C_ABI=0
export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0

target="${BACKEND_TARGET:-$($tool detect_host_target 2>/dev/null || echo auto)}"
borrow_ir="$(normalize_lower_trim "${BORROW_IR:-mir}")"
generic_lowering="$(normalize_lower_trim "${GENERIC_LOWERING:-mir_hybrid}")"
generic_mode="$(normalize_lower_trim "${MIR_BORROW_GENERIC_MODE:-${GENERIC_MODE:-dict}}")"
generic_budget="${MIR_BORROW_GENERIC_SPEC_BUDGET:-${GENERIC_SPEC_BUDGET:-0}}"
skip_sem="${MIR_BORROW_SKIP_SEM:-0}"
skip_ownership="${MIR_BORROW_SKIP_OWNERSHIP:-0}"
borrow_fixture="${MIR_BORROW_FIXTURE:-tests/cheng/backend/fixtures/return_add.cheng}"
generic_fixture="${MIR_BORROW_GENERIC_FIXTURE:-tests/cheng/backend/fixtures/return_new_expr_generic_box.cheng}"
if [ "${MIR_BORROW_FIXTURE:-}" = "" ] && [ ! -f "$borrow_fixture" ]; then
  borrow_fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi

skip_sem_norm="0"
if is_true_like "$skip_sem"; then
  skip_sem_norm="1"
fi
skip_ownership_norm="0"
if is_true_like "$skip_ownership"; then
  echo "[verify_backend_mir_borrow] MIR_BORROW_SKIP_OWNERSHIP is removed; ownership is fixed=0" 1>&2
  exit 2
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
proof_phase_driver_pick \
  "verify_backend_mir_borrow" \
  "$driver" \
  "$target" \
  "$out_dir" \
  "${BACKEND_PROOF_PHASE_PREFLIGHT_FIXTURE:-tests/cheng/backend/fixtures/return_i64.cheng}"
phase_driver="$proof_phase_driver_path"
phase_driver_surface="$proof_phase_driver_surface"
phase_proof_driver="${proof_phase_driver_proof_path:-$proof_phase_driver_path}"
phase_proof_surface="${proof_phase_driver_proof_surface:-$proof_phase_driver_surface}"
driver_real_env="$proof_phase_driver_env"
phase_driver_summary="$(proof_phase_driver_summary_flat)"
driver="$phase_driver"
if [ "$phase_driver" = "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2" ] &&
   [ -x "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof" ]; then
  driver="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2.proof"
fi
borrow_obj="$out_dir/mir_borrow_check.bin"
borrow_log="$out_dir/mir_borrow_check.log"
borrow_stamp="$out_dir/mir_borrow_check.compile_stamp.txt"
generic_obj="$out_dir/mir_borrow_generic_check.bin"
generic_log="$out_dir/mir_borrow_generic_check.log"
generic_stamp="$out_dir/mir_borrow_generic_check.compile_stamp.txt"
ownership_on_obj="$out_dir/mir_borrow_ownership_on_check.bin"
ownership_on_log="$out_dir/mir_borrow_ownership_on_check.log"
ownership_on_stamp="$out_dir/mir_borrow_ownership_on_check.compile_stamp.txt"
default_policy_obj="$out_dir/mir_borrow_default_policy_check.bin"
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
      $driver_real_env \
      UIR_PROFILE=1 \
      BACKEND_DEBUG_STAGE1_PIPE=1 \
      BACKEND_OPT_LEVEL=2 \
      BACKEND_OPT=1 \
      BACKEND_OPT2=1 \
      BORROW_IR="$borrow_ir" \
      GENERIC_LOWERING="$generic_lowering" \
      GENERIC_MODE="$generic_mode" \
      GENERIC_SPEC_BUDGET="$generic_budget" \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      STAGE1_SEM_FIXED_0="$skip_sem_norm" \
      BACKEND_LINKER=self \
      BACKEND_DIRECT_EXE=1 \
      BACKEND_LINKERLESS_INMEM=1 \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1
  elif [ "$ownership_mode" = "forced_on" ]; then
    env \
      $driver_real_env \
      UIR_PROFILE=1 \
      BACKEND_DEBUG_STAGE1_PIPE=1 \
      BACKEND_OPT_LEVEL=2 \
      BACKEND_OPT=1 \
      BACKEND_OPT2=1 \
      BORROW_IR="$borrow_ir" \
      GENERIC_LOWERING="$generic_lowering" \
      GENERIC_MODE="$generic_mode" \
      GENERIC_SPEC_BUDGET="$generic_budget" \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      STAGE1_SEM_FIXED_0="$skip_sem_norm" \
      STAGE1_OWNERSHIP_FIXED_0=0 \
      BACKEND_LINKER=self \
      BACKEND_DIRECT_EXE=1 \
      BACKEND_LINKERLESS_INMEM=1 \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_FRONTEND=stage1 \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$out_obj" \
      "$driver" >"$out_log" 2>&1
  else
    env \
      $driver_real_env \
      UIR_PROFILE=1 \
      BACKEND_DEBUG_STAGE1_PIPE=1 \
      BACKEND_OPT_LEVEL=2 \
      BACKEND_OPT=1 \
      BACKEND_OPT2=1 \
      BORROW_IR="$borrow_ir" \
      GENERIC_LOWERING="$generic_lowering" \
      GENERIC_MODE="$generic_mode" \
      GENERIC_SPEC_BUDGET="$generic_budget" \
      BACKEND_COMPILE_STAMP_OUT="$out_stamp" \
      STAGE1_SEM_FIXED_0="$skip_sem_norm" \
      STAGE1_OWNERSHIP_FIXED_0="$skip_ownership_norm" \
      BACKEND_LINKER=self \
      BACKEND_DIRECT_EXE=1 \
      BACKEND_LINKERLESS_INMEM=1 \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_EMIT=exe \
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

effective_generic_lowering="$(extract_stamp_field "$generic_stamp" "generic_lowering")"
if [ "$effective_generic_lowering" = "" ]; then
  echo "[verify_backend_mir_borrow] compile stamp missing generic_lowering: $generic_stamp" 1>&2
  exit 1
fi

require_stamp_field_exact "$generic_stamp" "borrow_ir" "$borrow_ir"
require_stamp_field_exact "$generic_stamp" "generic_lowering" "$effective_generic_lowering"
require_stamp_field_exact "$generic_stamp" "generic_mode" "$generic_mode"
require_stamp_field_exact "$generic_stamp" "generic_spec_budget" "$generic_budget"
require_stamp_field_exact "$ownership_on_stamp" "borrow_ir" "$borrow_ir"
require_stamp_field_exact "$ownership_on_stamp" "generic_lowering" "$effective_generic_lowering"
require_stamp_field_exact "$ownership_on_stamp" "generic_mode" "$generic_mode"
require_stamp_field_exact "$ownership_on_stamp" "generic_spec_budget" "$generic_budget"
require_stamp_field_exact "$default_policy_stamp" "borrow_ir" "$borrow_ir"
require_stamp_field_exact "$default_policy_stamp" "generic_lowering" "$effective_generic_lowering"
require_stamp_field_exact "$default_policy_stamp" "generic_mode" "$generic_mode"
require_stamp_field_exact "$default_policy_stamp" "generic_spec_budget" "$generic_budget"
require_stamp_field_exact "$generic_stamp" "uir_phase_contract_version" "p4_phase_v1"
require_stamp_field_exact "$ownership_on_stamp" "uir_phase_contract_version" "p4_phase_v1"
require_stamp_field_exact "$default_policy_stamp" "uir_phase_contract_version" "p4_phase_v1"

phase_contract_fields_mode="runtime_v2"
if grep -Fq "uir_phase_model=" "$generic_stamp"; then
  require_stamp_field_exact "$generic_stamp" "uir_phase_model" "single_ir_dual_phase"
  require_stamp_field_exact "$generic_stamp" "uir_high_phase_contract" "ownership_func_v1"
  require_stamp_field_exact "$ownership_on_stamp" "uir_phase_model" "single_ir_dual_phase"
  require_stamp_field_exact "$ownership_on_stamp" "uir_high_phase_contract" "ownership_func_v1"
  require_stamp_field_exact "$default_policy_stamp" "uir_phase_model" "single_ir_dual_phase"
  require_stamp_field_exact "$default_policy_stamp" "uir_high_phase_contract" "ownership_func_v1"
else
  phase_contract_fields_mode="source_contract_fallback"
  if ! grep -Fq '# uir_phase_model=single_ir_dual_phase' src/backend/tooling/backend_driver.cheng; then
    echo "[verify_backend_mir_borrow] backend_driver source missing uir_phase_model marker" 1>&2
    exit 1
  fi
  if ! grep -Fq '# ownership_func_v1' src/backend/tooling/backend_driver.cheng; then
    echo "[verify_backend_mir_borrow] backend_driver source missing ownership_func_v1 marker" 1>&2
    exit 1
  fi
  if ! grep -Fq 'uir_phase_contract_version=p4_phase_v1' src/backend/tooling/backend_driver_uir_sidecar_bundle.c; then
    echo "[verify_backend_mir_borrow] sidecar bundle source missing phase contract version marker" 1>&2
    exit 1
  fi
fi

compile_stamp_policy_mode="runtime_v2"
if grep -Fq "stage1_skip_sem_raw=" "$generic_stamp"; then
  require_stamp_field_falsey "$generic_stamp" "stage1_skip_sem_raw"
  require_stamp_field_exact "$generic_stamp" "stage1_skip_sem_effective" "$skip_sem_norm"
  require_stamp_field_falsey "$ownership_on_stamp" "stage1_skip_sem_raw"
  require_stamp_field_exact "$ownership_on_stamp" "stage1_skip_sem_effective" "$skip_sem_norm"
  require_stamp_field_falsey "$default_policy_stamp" "stage1_skip_sem_raw"
  require_stamp_field_exact "$default_policy_stamp" "stage1_skip_sem_effective" "$skip_sem_norm"
elif grep -Fq "stage1_ownership_fixed_0_effective=" "$generic_stamp" || \
     grep -Fq "stage1_skip_ownership_effective=" "$generic_stamp"; then
  compile_stamp_policy_mode="runtime_v1_ownership_only"
else
  compile_stamp_policy_mode="source_contract_fallback"
  if ! grep -Fq 'stage1_ownership_fixed_0_effective=0' src/backend/tooling/backend_driver_uir_sidecar_bundle.c; then
    echo "[verify_backend_mir_borrow] sidecar bundle source missing stage1_ownership_fixed_0_effective marker" 1>&2
    exit 1
  fi
  if ! grep -Fq 'stage1_ownership_fixed_0_default=0' src/backend/tooling/backend_driver_uir_sidecar_bundle.c; then
    echo "[verify_backend_mir_borrow] sidecar bundle source missing stage1_ownership_fixed_0_default marker" 1>&2
    exit 1
  fi
fi

require_ownership_fixed_0_field_exact "$generic_stamp" "effective" "$skip_ownership_norm"
require_ownership_fixed_0_field_exact "$generic_stamp" "default" "0"
require_ownership_fixed_0_field_exact "$ownership_on_stamp" "effective" "0"
require_ownership_fixed_0_field_exact "$ownership_on_stamp" "default" "0"
require_ownership_fixed_0_field_exact "$default_policy_stamp" "effective" "0"
require_ownership_fixed_0_field_exact "$default_policy_stamp" "default" "0"

phase_surface_mode="noalias_ssu_runtime"
report_phase_contract_version="p4_phase_v1"

for entry in \
  "$borrow_log:explicit_skip_ownership" \
  "$generic_log:generic_probe" \
  "$ownership_on_log:ownership_enabled" \
  "$default_policy_log:default_skip_ownership"; do
  log_file="${entry%%:*}"
  log_label="${entry#*:}"
  require_log_pattern "$log_file" '^noalias_report[[:space:]]+' "noalias_report ($log_label)"
  require_log_pattern "$log_file" '^ssu_report[[:space:]]+' "ssu_report ($log_label)"
  require_log_pattern "$log_file" '^uir_profile[[:space:]]+uir_opt2\.noalias([[:space:]]+|$)' "uir_opt2.noalias profile ($log_label)"
  require_log_pattern "$log_file" '^uir_profile[[:space:]]+uir_opt2\.ssu([[:space:]]+|$)' "uir_opt2.ssu profile ($log_label)"
  require_log_pattern "$log_file" '^uir_profile[[:space:]]+uir_opt2\.safe\.copy_prop([[:space:]]+|$)' "uir_opt2.safe.copy_prop profile ($log_label)"
  require_log_pattern "$log_file" '^uir_profile[[:space:]]+uir_opt2\.egraph([[:space:]]+|$)' "uir_opt2.egraph profile ($log_label)"
done

borrow_noalias_info="$(parse_noalias_report "$borrow_log" "explicit_skip_ownership")"
IFS='|' read -r borrow_noalias_line borrow_proof_checked_funcs borrow_proof_required borrow_unknown_slot_clobbers borrow_unknown_global_clobbers <<EOF
$borrow_noalias_info
EOF
generic_noalias_info="$(parse_noalias_report "$generic_log" "generic_probe")"
IFS='|' read -r generic_noalias_line generic_proof_checked_funcs generic_proof_required generic_unknown_slot_clobbers generic_unknown_global_clobbers <<EOF
$generic_noalias_info
EOF
ownership_on_noalias_info="$(parse_noalias_report "$ownership_on_log" "ownership_enabled")"
IFS='|' read -r ownership_on_noalias_line ownership_on_proof_checked_funcs ownership_on_proof_required ownership_on_unknown_slot_clobbers ownership_on_unknown_global_clobbers <<EOF
$ownership_on_noalias_info
EOF
default_policy_noalias_info="$(parse_noalias_report "$default_policy_log" "default_skip_ownership")"
IFS='|' read -r default_policy_noalias_line default_policy_proof_checked_funcs default_policy_proof_required default_policy_unknown_slot_clobbers default_policy_unknown_global_clobbers <<EOF
$default_policy_noalias_info
EOF

borrow_ssu_info="$(parse_ssu_report "$borrow_log" "explicit_skip_ownership")"
IFS='|' read -r borrow_ssu_line borrow_ssu_enabled borrow_tracked_funcs borrow_dup_candidates borrow_move_candidates borrow_use_version_max <<EOF
$borrow_ssu_info
EOF
generic_ssu_info="$(parse_ssu_report "$generic_log" "generic_probe")"
IFS='|' read -r generic_ssu_line generic_ssu_enabled generic_tracked_funcs generic_dup_candidates generic_move_candidates generic_use_version_max <<EOF
$generic_ssu_info
EOF
ownership_on_ssu_info="$(parse_ssu_report "$ownership_on_log" "ownership_enabled")"
IFS='|' read -r ownership_on_ssu_line ownership_on_ssu_enabled ownership_on_tracked_funcs ownership_on_dup_candidates ownership_on_move_candidates ownership_on_use_version_max <<EOF
$ownership_on_ssu_info
EOF
default_policy_ssu_info="$(parse_ssu_report "$default_policy_log" "default_skip_ownership")"
IFS='|' read -r default_policy_ssu_line default_policy_ssu_enabled default_policy_tracked_funcs default_policy_dup_candidates default_policy_move_candidates default_policy_use_version_max <<EOF
$default_policy_ssu_info
EOF

for proof_metric in \
  "$borrow_proof_checked_funcs:explicit_skip_ownership" \
  "$generic_proof_checked_funcs:generic_probe" \
  "$ownership_on_proof_checked_funcs:ownership_enabled" \
  "$default_policy_proof_checked_funcs:default_skip_ownership"; do
  proof_value="${proof_metric%%:*}"
  proof_label="${proof_metric#*:}"
  if [ "$proof_value" -le 0 ]; then
    echo "[verify_backend_mir_borrow] expected proof_checked_funcs>0 ($proof_label), got $proof_value" 1>&2
    exit 1
  fi
done

for ssu_metric in \
  "$borrow_ssu_enabled:$borrow_tracked_funcs:explicit_skip_ownership" \
  "$generic_ssu_enabled:$generic_tracked_funcs:generic_probe" \
  "$ownership_on_ssu_enabled:$ownership_on_tracked_funcs:ownership_enabled" \
  "$default_policy_ssu_enabled:$default_policy_tracked_funcs:default_skip_ownership"; do
  ssu_enabled_value="${ssu_metric%%:*}"
  rest="${ssu_metric#*:}"
  tracked_value="${rest%%:*}"
  ssu_label="${rest#*:}"
  if [ "$ssu_enabled_value" != "1" ]; then
    echo "[verify_backend_mir_borrow] expected ssu enabled=1 ($ssu_label), got $ssu_enabled_value" 1>&2
    exit 1
  fi
  if [ "$tracked_value" -le 0 ]; then
    echo "[verify_backend_mir_borrow] expected tracked_funcs>0 ($ssu_label), got $tracked_value" 1>&2
    exit 1
  fi
done

if [ "$borrow_move_candidates" != "$default_policy_move_candidates" ]; then
  echo "[verify_backend_mir_borrow] explicit/default borrow move_candidates diverged: explicit=$borrow_move_candidates default=$default_policy_move_candidates" 1>&2
  exit 1
fi
if [ "$borrow_use_version_max" != "$default_policy_use_version_max" ]; then
  echo "[verify_backend_mir_borrow] explicit/default borrow use_version_max diverged: explicit=$borrow_use_version_max default=$default_policy_use_version_max" 1>&2
  exit 1
fi
if [ "$generic_move_candidates" != "$ownership_on_move_candidates" ]; then
  echo "[verify_backend_mir_borrow] generic/ownership-on move_candidates diverged: generic=$generic_move_candidates ownership_on=$ownership_on_move_candidates" 1>&2
  exit 1
fi
if [ "$generic_use_version_max" != "$ownership_on_use_version_max" ]; then
  echo "[verify_backend_mir_borrow] generic/ownership-on use_version_max diverged: generic=$generic_use_version_max ownership_on=$ownership_on_use_version_max" 1>&2
  exit 1
fi
if [ "$generic_move_candidates" -le "$borrow_move_candidates" ]; then
  echo "[verify_backend_mir_borrow] generic probe did not increase move_candidates over simple borrow fixture: generic=$generic_move_candidates borrow=$borrow_move_candidates" 1>&2
  exit 1
fi
if [ "$generic_use_version_max" -le "$borrow_use_version_max" ]; then
  echo "[verify_backend_mir_borrow] generic probe did not increase use_version_max over simple borrow fixture: generic=$generic_use_version_max borrow=$borrow_use_version_max" 1>&2
  exit 1
fi
if [ "$generic_move_candidates" -le 0 ]; then
  echo "[verify_backend_mir_borrow] expected generic probe move_candidates>0, got $generic_move_candidates" 1>&2
  exit 1
fi
if [ "$generic_use_version_max" -le 0 ]; then
  echo "[verify_backend_mir_borrow] expected generic probe use_version_max>0, got $generic_use_version_max" 1>&2
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
  echo "phase_driver=$phase_driver"
  echo "phase_driver_surface=$phase_driver_surface"
  echo "phase_proof_driver=$phase_proof_driver"
  echo "phase_proof_surface=$phase_proof_surface"
  echo "phase_driver_summary=$phase_driver_summary"
  echo "target=$target"
  echo "borrow_ir=$borrow_ir"
  echo "generic_lowering=$generic_lowering"
  echo "effective_generic_lowering=$effective_generic_lowering"
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
  echo "compile_stamp_status=present"
  echo "compile_stamp_policy_mode=$compile_stamp_policy_mode"
  echo "phase_contract_fields_mode=$phase_contract_fields_mode"
  echo "phase_surface_mode=$phase_surface_mode"
  echo "borrow_noalias_report=$borrow_noalias_line"
  echo "generic_noalias_report=$generic_noalias_line"
  echo "ownership_on_noalias_report=$ownership_on_noalias_line"
  echo "default_policy_noalias_report=$default_policy_noalias_line"
  echo "borrow_ssu_report=$borrow_ssu_line"
  echo "generic_ssu_report=$generic_ssu_line"
  echo "ownership_on_ssu_report=$ownership_on_ssu_line"
  echo "default_policy_ssu_report=$default_policy_ssu_line"
  echo "borrow_proof_checked_funcs=$borrow_proof_checked_funcs"
  echo "generic_proof_checked_funcs=$generic_proof_checked_funcs"
  echo "ownership_on_proof_checked_funcs=$ownership_on_proof_checked_funcs"
  echo "default_policy_proof_checked_funcs=$default_policy_proof_checked_funcs"
  echo "borrow_tracked_funcs=$borrow_tracked_funcs"
  echo "generic_tracked_funcs=$generic_tracked_funcs"
  echo "ownership_on_tracked_funcs=$ownership_on_tracked_funcs"
  echo "default_policy_tracked_funcs=$default_policy_tracked_funcs"
  echo "borrow_move_candidates=$borrow_move_candidates"
  echo "generic_move_candidates=$generic_move_candidates"
  echo "ownership_on_move_candidates=$ownership_on_move_candidates"
  echo "default_policy_move_candidates=$default_policy_move_candidates"
  echo "borrow_use_version_max=$borrow_use_version_max"
  echo "generic_use_version_max=$generic_use_version_max"
  echo "ownership_on_use_version_max=$ownership_on_use_version_max"
  echo "default_policy_use_version_max=$default_policy_use_version_max"
  echo "phase_contract_version=$report_phase_contract_version"
} > "$snapshot_file"

{
  echo "verify_backend_mir_borrow report"
  echo "driver=$driver"
  echo "phase_driver=$phase_driver"
  echo "phase_driver_surface=$phase_driver_surface"
  echo "phase_proof_driver=$phase_proof_driver"
  echo "phase_proof_surface=$phase_proof_surface"
  echo "phase_driver_summary=$phase_driver_summary"
  echo "target=$target"
  echo "borrow_ir=$borrow_ir"
  echo "generic_lowering=$generic_lowering"
  echo "effective_generic_lowering=$effective_generic_lowering"
  echo "generic_mode=$generic_mode"
  echo "generic_budget=$generic_budget"
  echo "skip_sem=$skip_sem_norm"
  echo "skip_ownership=$skip_ownership_norm"
  echo "borrow_fixture=$borrow_fixture"
  echo "generic_fixture=$generic_fixture"
  echo "phase_surface_mode=$phase_surface_mode"
  echo "borrow_noalias_report=$borrow_noalias_line"
  echo "generic_noalias_report=$generic_noalias_line"
  echo "ownership_on_noalias_report=$ownership_on_noalias_line"
  echo "default_policy_noalias_report=$default_policy_noalias_line"
  echo "borrow_ssu_report=$borrow_ssu_line"
  echo "generic_ssu_report=$generic_ssu_line"
  echo "ownership_on_ssu_report=$ownership_on_ssu_line"
  echo "default_policy_ssu_report=$default_policy_ssu_line"
  echo "borrow_proof_checked_funcs=$borrow_proof_checked_funcs"
  echo "generic_proof_checked_funcs=$generic_proof_checked_funcs"
  echo "ownership_on_proof_checked_funcs=$ownership_on_proof_checked_funcs"
  echo "default_policy_proof_checked_funcs=$default_policy_proof_checked_funcs"
  echo "borrow_tracked_funcs=$borrow_tracked_funcs"
  echo "generic_tracked_funcs=$generic_tracked_funcs"
  echo "ownership_on_tracked_funcs=$ownership_on_tracked_funcs"
  echo "default_policy_tracked_funcs=$default_policy_tracked_funcs"
  echo "borrow_move_candidates=$borrow_move_candidates"
  echo "generic_move_candidates=$generic_move_candidates"
  echo "ownership_on_move_candidates=$ownership_on_move_candidates"
  echo "default_policy_move_candidates=$default_policy_move_candidates"
  echo "borrow_use_version_max=$borrow_use_version_max"
  echo "generic_use_version_max=$generic_use_version_max"
  echo "ownership_on_use_version_max=$ownership_on_use_version_max"
  echo "default_policy_use_version_max=$default_policy_use_version_max"
  echo "phase_contract_version=$report_phase_contract_version"
  echo "compile_stamp=$generic_stamp"
  echo "ownership_on_compile_stamp=$ownership_on_stamp"
  echo "default_policy_compile_stamp=$default_policy_stamp"
  echo "compile_stamp_status=present"
  echo "compile_stamp_policy_mode=$compile_stamp_policy_mode"
  echo "phase_contract_fields_mode=$phase_contract_fields_mode"
} > "$report_file"

echo "verify_backend_mir_borrow ok"
