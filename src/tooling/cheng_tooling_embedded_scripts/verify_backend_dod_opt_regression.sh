#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"
. "$root/src/tooling/cheng_tooling_embedded_scripts/proof_phase_driver_common.sh"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_dod_opt_regression] rg is required" 1>&2
  exit 2
fi

extract_report_field() {
  line="$1"
  key="$2"
  printf '%s\n' "$line" | tr '\t' '\n' | awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }'
}

extract_kv_field() {
  line="$1"
  key="$2"
  printf '%s\n' "$line" | awk -v key="$key" '
    {
      for (i = 1; i <= NF; ++i) {
        if (index($i, key "=") == 1) {
          print substr($i, length(key) + 2)
          exit
        }
      }
    }'
}

extract_stamp_field() {
  stamp_file="$1"
  key="$2"
  if [ ! -f "$stamp_file" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$stamp_file"
}

extract_phase_field() {
  log_file="$1"
  key="$2"
  line="$(rg '^generics_report[[:space:]]+' "$log_file" | tail -n 1 || true)"
  if [ "$line" = "" ]; then
    line="$(rg '^\[backend\] stage1: phase_contract_done ' "$log_file" | tail -n 1 || true)"
  fi
  if [ "$line" = "" ]; then
    printf '\n'
    return 0
  fi
  extract_kv_field "$line" "$key"
}

profile_label_int_value() {
  log_file="$1"
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
      if (!found) {
        print ""
      }
    }
  ' "$log_file"
}

is_uint() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

out_dir="artifacts/backend_dod_opt_regression"
rm -rf "$out_dir"
mkdir -p "$out_dir"
report="$out_dir/verify_backend_dod_opt_regression.report.txt"
snapshot="$out_dir/verify_backend_dod_opt_regression.snapshot.env"

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
if [ ! -x "$tool" ]; then
  echo "[verify_backend_dod_opt_regression] missing tooling binary: $tool" 1>&2
  exit 1
fi

driver="$($tool driver-path --path-only 2>/dev/null | tail -n 1 | tr -d '\r' || true)"
if [ "$driver" = "" ]; then
  driver="artifacts/backend_driver/cheng"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_dod_opt_regression] backend driver not executable: $driver" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-$($tool detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
proof_phase_driver_pick \
  "verify_backend_dod_opt_regression" \
  "$driver" \
  "$target" \
  "$out_dir" \
  "${BACKEND_PROOF_PHASE_PREFLIGHT_FIXTURE:-tests/cheng/backend/fixtures/return_i64.cheng}"
phase_driver="$proof_phase_driver_path"
phase_driver_surface="$proof_phase_driver_surface"
driver_exec="$phase_driver"
driver_real_env="$proof_phase_driver_env"
mode="runtime"
report_mode="runtime_phase_surface"

source_ok=1
for file in \
  src/backend/uir/uir_noalias_pass.cheng \
  src/backend/uir/uir_egraph_cost.cheng \
  src/backend/uir/uir_opt.cheng \
  src/backend/tooling/backend_driver.cheng; do
  if [ ! -f "$file" ]; then
    source_ok=0
  fi
done
for pat in 'uir_opt2\.noalias' 'uir_opt2\.egraph' 'single_ir_dual_phase' 'p4_phase_v1'; do
  if ! rg -q "$pat" src/backend/uir/uir_opt.cheng src/backend/tooling/backend_driver.cheng; then
    source_ok=0
  fi
done
if [ "$source_ok" != "1" ]; then
  echo "[verify_backend_dod_opt_regression] source contract markers missing" 1>&2
  exit 1
fi

fixture_list="${BACKEND_DOD_OPT_FIXTURES:-tests/cheng/backend/fixtures/return_opt2_sroa_deref.cheng;tests/cheng/backend/fixtures/return_opt2_noalias_mem2reg_load.cheng;tests/cheng/backend/fixtures/return_opt2_inline_dce.cheng;tests/cheng/backend/fixtures/return_opt2_algebraic.cse.cheng}"
fixture_lines="$(printf '%s\n' "$fixture_list" | tr ';' '\n' | tr '\r' '\n' | sed -e '/^[[:space:]]*$/d')"
min_probe_count="${BACKEND_DOD_OPT_MIN_PROBE_COUNT:-3}"
case "$min_probe_count" in
  ''|*[!0-9]*) min_probe_count=3 ;;
esac

probe_count=0
proof_backed_changes_total=0
proof_checked_funcs_total=0
proof_skipped_funcs_total=0
proof_required_total=0
proof_backed_egraph_funcs_total=0
egraph_report_count_total=0
changed_funcs_total=0
unknown_slot_clobbers_total=0
unknown_global_clobbers_total=0
kill_events_total=0
high_uir_checked_total=0
high_uir_fallback_total=0
low_uir_lowered_total=0
skip_ownership_effective_nonzero_count=0
phase_contract_version_mismatch_count=0
phase_model_mismatch_count=0

while IFS= read -r fixture; do
  [ "$fixture" = "" ] && continue
  if [ ! -f "$fixture" ]; then
    continue
  fi
  probe_count=$((probe_count + 1))
  out_bin="$out_dir/dod_opt_probe.$probe_count.bin"
  log="$out_dir/dod_opt_probe.$probe_count.log"
  stamp="$out_dir/dod_opt_probe.$probe_count.compile_stamp.txt"
  rm -f "$out_bin" "$log" "$stamp"

  set +e
  env \
    $driver_real_env \
    UIR_PROFILE=1 \
    BACKEND_PROFILE=0 \
    BACKEND_OPT_LEVEL=2 \
    BACKEND_OPT=1 \
    BACKEND_OPT2=1 \
    BACKEND_DEBUG_STAGE1_PIPE=1 \
    BORROW_IR=mir \
    GENERIC_LOWERING=mir_hybrid \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    BACKEND_COMPILE_STAMP_OUT="$stamp" \
    STAGE1_SEM_FIXED_0=0 \
    STAGE1_OWNERSHIP_FIXED_0=0 \
    STAGE1_STD_NO_POINTERS=0 \
    STAGE1_STD_NO_POINTERS_STRICT=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    BACKEND_LINKER=self \
    BACKEND_DIRECT_EXE=1 \
    BACKEND_LINKERLESS_INMEM=1 \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$out_bin" \
    UIR_NOALIAS=1 \
    UIR_NOALIAS_REQUIRE_PROOF=1 \
    UIR_EGRAPH_ITERS=3 \
    UIR_EGRAPH_REQUIRE_PROOF=1 \
    UIR_EGRAPH_REPORT=1 \
    "$driver_exec" >"$log" 2>&1
  rc="$?"
  set -e

  if [ "$rc" -ne 0 ] || [ ! -s "$out_bin" ]; then
    echo "[verify_backend_dod_opt_regression] runtime probe compile failed fixture=$fixture" 1>&2
    sed -n '1,120p' "$log" 1>&2 || true
    exit 1
  fi
  if [ ! -s "$stamp" ]; then
    echo "[verify_backend_dod_opt_regression] missing compile stamp fixture=$fixture" 1>&2
    exit 1
  fi

  phase_model="$(extract_stamp_field "$stamp" "uir_phase_model")"
  phase_version="$(extract_stamp_field "$stamp" "uir_phase_contract_version")"
  skip_ownership_effective="$(extract_stamp_field "$stamp" "stage1_skip_ownership_effective")"
  high_uir_checked="$(extract_phase_field "$log" "high_uir_checked_funcs")"
  high_uir_fallback="$(extract_phase_field "$log" "high_uir_fallback_funcs")"
  low_uir_lowered="$(extract_phase_field "$log" "low_uir_lowered_funcs")"
  if ! rg -q '^(generics_report[[:space:]]+|\[backend\] stage1: phase_contract_done )' "$log"; then
    echo "[verify_backend_dod_opt_regression] phase contract marker missing fixture=$fixture" 1>&2
    exit 1
  fi
  for pair in \
    "skip_ownership_effective:$skip_ownership_effective" \
    "high_uir_checked:$high_uir_checked" \
    "high_uir_fallback:$high_uir_fallback" \
    "low_uir_lowered:$low_uir_lowered"; do
    key="${pair%%:*}"
    val="${pair#*:}"
    if ! is_uint "$val"; then
      echo "[verify_backend_dod_opt_regression] invalid phase metric (${key}) fixture=$fixture value=${val:-<empty>}" 1>&2
      exit 1
    fi
  done
  if [ "$phase_model" != "single_ir_dual_phase" ]; then
    phase_model_mismatch_count=$((phase_model_mismatch_count + 1))
  fi
  if [ "$phase_version" != "p4_phase_v1" ]; then
    phase_contract_version_mismatch_count=$((phase_contract_version_mismatch_count + 1))
  fi
  if [ "$skip_ownership_effective" -ne 0 ]; then
    skip_ownership_effective_nonzero_count=$((skip_ownership_effective_nonzero_count + 1))
  fi
  high_uir_checked_total=$((high_uir_checked_total + high_uir_checked))
  high_uir_fallback_total=$((high_uir_fallback_total + high_uir_fallback))
  low_uir_lowered_total=$((low_uir_lowered_total + low_uir_lowered))

  line="$(rg '^noalias_report' "$log" | tail -n 1 || true)"
  if [ "$line" = "" ]; then
    echo "[verify_backend_dod_opt_regression] noalias_report missing fixture=$fixture" 1>&2
    exit 1
  fi

  proof_backed_changes="$(extract_report_field "$line" "proof_backed_changes")"
  proof_checked_funcs="$(extract_report_field "$line" "proof_checked_funcs")"
  proof_skipped_funcs="$(extract_report_field "$line" "proof_skipped_funcs")"
  proof_required="$(extract_report_field "$line" "proof_required")"
  changed_funcs="$(extract_report_field "$line" "changed_funcs")"
  unknown_slot_clobbers="$(extract_report_field "$line" "unknown_slot_clobbers")"
  unknown_global_clobbers="$(extract_report_field "$line" "unknown_global_clobbers")"
  kill_events="$(extract_report_field "$line" "kill_events")"
  if [ "$unknown_slot_clobbers" = "" ]; then
    unknown_slot_clobbers=0
  fi
  if [ "$unknown_global_clobbers" = "" ]; then
    unknown_global_clobbers=0
  fi
  if [ "$kill_events" = "" ]; then
    kill_events=0
  fi
  for pair in \
    "proof_backed_changes:$proof_backed_changes" \
    "proof_checked_funcs:$proof_checked_funcs" \
    "proof_skipped_funcs:$proof_skipped_funcs" \
    "proof_required:$proof_required" \
    "changed_funcs:$changed_funcs" \
    "unknown_slot_clobbers:$unknown_slot_clobbers" \
    "unknown_global_clobbers:$unknown_global_clobbers" \
    "kill_events:$kill_events"; do
    key="${pair%%:*}"
    val="${pair#*:}"
    if ! is_uint "$val"; then
      echo "[verify_backend_dod_opt_regression] invalid noalias_report metric (${key}) fixture=$fixture value=${val:-<empty>}" 1>&2
      exit 1
    fi
  done

  proof_backed_egraph_funcs="$(profile_label_int_value "$log" "uir_opt2.egraph.proof_backed_funcs=")"
  if [ "$proof_backed_egraph_funcs" = "" ]; then
    proof_backed_egraph_funcs=0
  fi
  if ! is_uint "$proof_backed_egraph_funcs"; then
    echo "[verify_backend_dod_opt_regression] invalid egraph proof_backed_funcs fixture=$fixture value=${proof_backed_egraph_funcs:-<empty>}" 1>&2
    exit 1
  fi

  proof_backed_changes_total=$((proof_backed_changes_total + proof_backed_changes))
  proof_checked_funcs_total=$((proof_checked_funcs_total + proof_checked_funcs))
  proof_skipped_funcs_total=$((proof_skipped_funcs_total + proof_skipped_funcs))
  proof_required_total=$((proof_required_total + proof_required))
  proof_backed_egraph_funcs_total=$((proof_backed_egraph_funcs_total + proof_backed_egraph_funcs))
  egraph_report_count_total=$((egraph_report_count_total + $(rg -c '^egraph_report' "$log" 2>/dev/null || echo 0)))
  changed_funcs_total=$((changed_funcs_total + changed_funcs))
  unknown_slot_clobbers_total=$((unknown_slot_clobbers_total + unknown_slot_clobbers))
  unknown_global_clobbers_total=$((unknown_global_clobbers_total + unknown_global_clobbers))
  kill_events_total=$((kill_events_total + kill_events))
done <<EOF
$fixture_lines
EOF

if [ "$probe_count" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] missing fixture (none from list exist)" 1>&2
  exit 2
fi
if [ "$probe_count" -lt "$min_probe_count" ]; then
  echo "[verify_backend_dod_opt_regression] insufficient probe coverage: probe_count=$probe_count min=$min_probe_count" 1>&2
  exit 1
fi
if [ "$phase_model_mismatch_count" -gt 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase model mismatch across proof probes: count=$phase_model_mismatch_count expected=single_ir_dual_phase" 1>&2
  exit 1
fi
if [ "$phase_contract_version_mismatch_count" -gt 0 ]; then
  echo "[verify_backend_dod_opt_regression] phase contract version mismatch across proof probes: count=$phase_contract_version_mismatch_count expected=p4_phase_v1" 1>&2
  exit 1
fi
if [ "$skip_ownership_effective_nonzero_count" -gt 0 ]; then
  echo "[verify_backend_dod_opt_regression] ownership proof path not engaged: stage1_skip_ownership_effective!=0 count=$skip_ownership_effective_nonzero_count" 1>&2
  exit 1
fi
if [ "$high_uir_checked_total" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] no high_uir_checked_funcs observed across proof probes" 1>&2
  exit 1
fi
if [ "$proof_checked_funcs_total" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] no noalias proof_checked_funcs observed across proof probes" 1>&2
  exit 1
fi
if [ "$proof_backed_changes_total" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] no proof-backed noalias changes observed" 1>&2
  exit 1
fi
if [ "$proof_backed_egraph_funcs_total" -le 0 ]; then
  echo "[verify_backend_dod_opt_regression] no proof_backed egraph funcs observed" 1>&2
  exit 1
fi

{
  echo "status=ok"
  echo "mode=$mode"
  echo "report_mode=$report_mode"
  echo "phase_surface=compile_stamp+generics_report+profile"
  echo "driver=$driver"
  echo "phase_driver=$phase_driver"
  echo "phase_driver_surface=$phase_driver_surface"
  echo "driver_exec=$driver_exec"
  echo "min_probe_count=$min_probe_count"
  echo "probe_count=$probe_count"
  echo "high_uir_checked_total=$high_uir_checked_total"
  echo "high_uir_fallback_total=$high_uir_fallback_total"
  echo "low_uir_lowered_total=$low_uir_lowered_total"
  echo "proof_backed_changes_total=$proof_backed_changes_total"
  echo "proof_checked_funcs_total=$proof_checked_funcs_total"
  echo "proof_skipped_funcs_total=$proof_skipped_funcs_total"
  echo "proof_required_total=$proof_required_total"
  echo "proof_backed_egraph_funcs_total=$proof_backed_egraph_funcs_total"
  echo "egraph_report_count_total=$egraph_report_count_total"
  echo "changed_funcs_total=$changed_funcs_total"
  echo "unknown_slot_clobbers_total=$unknown_slot_clobbers_total"
  echo "unknown_global_clobbers_total=$unknown_global_clobbers_total"
  echo "kill_events_total=$kill_events_total"
} >"$report"

{
  echo "backend_dod_opt_regression_status=ok"
  echo "backend_dod_opt_regression_mode=$mode"
  echo "backend_dod_opt_regression_report_mode=$report_mode"
  echo "backend_dod_opt_regression_phase_surface=compile_stamp+generics_report+profile"
  echo "backend_dod_opt_regression_driver=$driver"
  echo "backend_dod_opt_regression_phase_driver=$phase_driver"
  echo "backend_dod_opt_regression_phase_driver_surface=$phase_driver_surface"
  echo "backend_dod_opt_regression_driver_exec=$driver_exec"
  echo "backend_dod_opt_regression_min_probe_count=$min_probe_count"
  echo "backend_dod_opt_regression_probe_count=$probe_count"
  echo "backend_dod_opt_regression_high_uir_checked_total=$high_uir_checked_total"
  echo "backend_dod_opt_regression_high_uir_fallback_total=$high_uir_fallback_total"
  echo "backend_dod_opt_regression_low_uir_lowered_total=$low_uir_lowered_total"
  echo "backend_dod_opt_regression_proof_backed_changes_total=$proof_backed_changes_total"
  echo "backend_dod_opt_regression_proof_checked_funcs_total=$proof_checked_funcs_total"
  echo "backend_dod_opt_regression_proof_skipped_funcs_total=$proof_skipped_funcs_total"
  echo "backend_dod_opt_regression_proof_required_total=$proof_required_total"
  echo "backend_dod_opt_regression_proof_backed_egraph_funcs_total=$proof_backed_egraph_funcs_total"
  echo "backend_dod_opt_regression_egraph_report_count_total=$egraph_report_count_total"
  echo "backend_dod_opt_regression_changed_funcs_total=$changed_funcs_total"
  echo "backend_dod_opt_regression_unknown_slot_clobbers_total=$unknown_slot_clobbers_total"
  echo "backend_dod_opt_regression_unknown_global_clobbers_total=$unknown_global_clobbers_total"
  echo "backend_dod_opt_regression_kill_events_total=$kill_events_total"
  echo "backend_dod_opt_regression_report=$report"
} >"$snapshot"

echo "verify_backend_dod_opt_regression ok"
