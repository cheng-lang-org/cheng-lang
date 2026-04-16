#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"
. "$root/src/tooling/cheng_tooling_embedded_scripts/proof_phase_driver_common.sh"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_noalias_opt] rg is required" 1>&2
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

extract_internal_ownership_fixed_0_effective() {
  stamp_file="$1"
  value="$(extract_stamp_field "$stamp_file" "stage1_ownership_fixed_0_effective")"
  if [ "$value" = "" ]; then
    value="$(extract_stamp_field "$stamp_file" "stage1_skip_ownership_effective")"
  fi
  printf '%s\n' "$value"
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

is_uint() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

out_dir="artifacts/backend_noalias_opt"
rm -rf "$out_dir"
mkdir -p "$out_dir"
report="$out_dir/verify_backend_noalias_opt.report.txt"
snapshot="$out_dir/verify_backend_noalias_opt.snapshot.env"

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
if [ ! -x "$tool" ]; then
  echo "[verify_backend_noalias_opt] missing tooling binary: $tool" 1>&2
  exit 1
fi

driver="$($tool driver-path --path-only 2>/dev/null | tail -n 1 | tr -d '\r' || true)"
if [ "$driver" = "" ]; then
  driver="artifacts/v3_backend_driver/cheng"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_noalias_opt] backend driver not executable: $driver" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-$($tool detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
proof_phase_driver_pick \
  "verify_backend_noalias_opt" \
  "$driver" \
  "$target" \
  "$out_dir" \
  "${BACKEND_PROOF_PHASE_PREFLIGHT_FIXTURE:-tests/cheng/backend/fixtures/return_i64.cheng}"
phase_driver="$proof_phase_driver_path"
phase_driver_surface="$proof_phase_driver_surface"
driver_exec="$phase_driver"
driver_real_env="$proof_phase_driver_env"
for pat in 'fn uirRunNoAliasPrep\(' 'unknown_slot_clobbers' 'unknown_global_clobbers' 'kill_events' 'proof_backed_changes='; do
  if ! rg -q "$pat" src/backend/uir/uir_noalias_pass.cheng; then
    echo "[verify_backend_noalias_opt] source contract markers missing: $pat" 1>&2
    exit 1
  fi
done

fixture_list="${BACKEND_NOALIAS_FIXTURES:-tests/cheng/backend/fixtures/return_opt2_noalias_mem2reg_load.cheng;tests/cheng/backend/fixtures/return_opt2_sroa_deref.cheng;tests/cheng/backend/fixtures/return_opt2_inline_dce.cheng;tests/cheng/backend/fixtures/return_opt2_cse.cheng}"
fixture_lines="$(printf '%s\n' "$fixture_list" | tr ';' '\n' | tr '\r' '\n' | sed -e '/^[[:space:]]*$/d')"
min_probe_count="${BACKEND_NOALIAS_MIN_PROBE_COUNT:-3}"
case "$min_probe_count" in
  ''|*[!0-9]*) min_probe_count=3 ;;
esac

probe_count=0
proof_backed_changes_total=0
mem2reg_loads_total=0
forward_loads_total=0
changed_funcs_total=0
unknown_slot_clobbers_total=0
unknown_global_clobbers_total=0
kill_events_total=0
proof_checked_funcs_total=0
proof_skipped_funcs_total=0
proof_required_total=0
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
  out_bin="$out_dir/noalias_probe.$probe_count.bin"
  log="$out_dir/noalias_probe.$probe_count.log"
  stamp="$out_dir/noalias_probe.$probe_count.compile_stamp.txt"
  rm -f "$out_bin" "$log" "$stamp"

  set +e
  env \
    $driver_real_env \
    UIR_PROFILE=0 \
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
    "$driver_exec" >"$log" 2>&1
  rc="$?"
  set -e

  if [ "$rc" -ne 0 ] || [ ! -s "$out_bin" ]; then
    echo "[verify_backend_noalias_opt] runtime probe compile failed fixture=$fixture" 1>&2
    sed -n '1,160p' "$log" 1>&2 || true
    exit 1
  fi
  if [ ! -s "$stamp" ]; then
    echo "[verify_backend_noalias_opt] missing compile stamp fixture=$fixture" 1>&2
    exit 1
  fi

  phase_model="$(extract_stamp_field "$stamp" "uir_phase_model")"
  phase_version="$(extract_stamp_field "$stamp" "uir_phase_contract_version")"
  skip_ownership_effective="$(extract_internal_ownership_fixed_0_effective "$stamp")"
  high_uir_checked="$(extract_phase_field "$log" "high_uir_checked_funcs")"
  high_uir_fallback="$(extract_phase_field "$log" "high_uir_fallback_funcs")"
  low_uir_lowered="$(extract_phase_field "$log" "low_uir_lowered_funcs")"
  if ! rg -q '^(generics_report[[:space:]]+|\[backend\] stage1: phase_contract_done )' "$log"; then
    echo "[verify_backend_noalias_opt] phase contract marker missing fixture=$fixture" 1>&2
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
      echo "[verify_backend_noalias_opt] invalid phase metric (${key}) fixture=$fixture value=${val:-<empty>}" 1>&2
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
    echo "[verify_backend_noalias_opt] noalias_report missing in runtime probe fixture=$fixture" 1>&2
    exit 1
  fi

  proof_backed_changes="$(extract_report_field "$line" "proof_backed_changes")"
  mem2reg_loads="$(extract_report_field "$line" "mem2reg_loads")"
  forward_loads="$(extract_report_field "$line" "forward_loads")"
  changed_funcs="$(extract_report_field "$line" "changed_funcs")"
  unknown_slot_clobbers="$(extract_report_field "$line" "unknown_slot_clobbers")"
  unknown_global_clobbers="$(extract_report_field "$line" "unknown_global_clobbers")"
  kill_events="$(extract_report_field "$line" "kill_events")"
  proof_checked_funcs="$(extract_report_field "$line" "proof_checked_funcs")"
  proof_skipped_funcs="$(extract_report_field "$line" "proof_skipped_funcs")"
  proof_required="$(extract_report_field "$line" "proof_required")"
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
    "mem2reg_loads:$mem2reg_loads" \
    "forward_loads:$forward_loads" \
    "changed_funcs:$changed_funcs" \
    "unknown_slot_clobbers:$unknown_slot_clobbers" \
    "unknown_global_clobbers:$unknown_global_clobbers" \
    "kill_events:$kill_events" \
    "proof_checked_funcs:$proof_checked_funcs" \
    "proof_skipped_funcs:$proof_skipped_funcs" \
    "proof_required:$proof_required"; do
    key="${pair%%:*}"
    val="${pair#*:}"
    if ! is_uint "$val"; then
      echo "[verify_backend_noalias_opt] invalid noalias_report metric (${key}) fixture=$fixture value=${val:-<empty>}" 1>&2
      exit 1
    fi
  done

  proof_backed_changes_total=$((proof_backed_changes_total + proof_backed_changes))
  mem2reg_loads_total=$((mem2reg_loads_total + mem2reg_loads))
  forward_loads_total=$((forward_loads_total + forward_loads))
  changed_funcs_total=$((changed_funcs_total + changed_funcs))
  unknown_slot_clobbers_total=$((unknown_slot_clobbers_total + unknown_slot_clobbers))
  unknown_global_clobbers_total=$((unknown_global_clobbers_total + unknown_global_clobbers))
  kill_events_total=$((kill_events_total + kill_events))
  proof_checked_funcs_total=$((proof_checked_funcs_total + proof_checked_funcs))
  proof_skipped_funcs_total=$((proof_skipped_funcs_total + proof_skipped_funcs))
  proof_required_total=$((proof_required_total + proof_required))
done <<EOF
$fixture_lines
EOF

if [ "$probe_count" -le 0 ]; then
  echo "[verify_backend_noalias_opt] missing fixture (none from list exist)" 1>&2
  exit 2
fi
if [ "$probe_count" -lt "$min_probe_count" ]; then
  echo "[verify_backend_noalias_opt] insufficient probe coverage: probe_count=$probe_count min=$min_probe_count" 1>&2
  exit 1
fi
if [ "$phase_model_mismatch_count" -gt 0 ]; then
  echo "[verify_backend_noalias_opt] phase model mismatch across proof probes: count=$phase_model_mismatch_count expected=single_ir_dual_phase" 1>&2
  exit 1
fi
if [ "$phase_contract_version_mismatch_count" -gt 0 ]; then
  echo "[verify_backend_noalias_opt] phase contract version mismatch across proof probes: count=$phase_contract_version_mismatch_count expected=p4_phase_v1" 1>&2
  exit 1
fi
if [ "$skip_ownership_effective_nonzero_count" -gt 0 ]; then
  echo "[verify_backend_noalias_opt] ownership proof path not engaged: stage1_ownership_fixed_0_effective!=0 count=$skip_ownership_effective_nonzero_count" 1>&2
  exit 1
fi
if [ "$high_uir_checked_total" -le 0 ]; then
  echo "[verify_backend_noalias_opt] no high_uir_checked_funcs observed across proof probes" 1>&2
  exit 1
fi
if [ "$proof_checked_funcs_total" -le 0 ]; then
  echo "[verify_backend_noalias_opt] no proof_checked_funcs observed across noalias proof probes" 1>&2
  exit 1
fi
if [ "$proof_backed_changes_total" -le 0 ]; then
  echo "[verify_backend_noalias_opt] no proof-backed noalias changes observed" 1>&2
  exit 1
fi

{
  echo "status=ok"
  echo "mode=runtime"
  echo "phase_surface=compile_stamp+generics_report+noalias_report"
  echo "driver=$driver"
  echo "phase_driver=$phase_driver"
  echo "phase_driver_surface=$phase_driver_surface"
  echo "driver_exec=$driver_exec"
  echo "target=$target"
  echo "min_probe_count=$min_probe_count"
  echo "probe_count=$probe_count"
  echo "high_uir_checked_total=$high_uir_checked_total"
  echo "high_uir_fallback_total=$high_uir_fallback_total"
  echo "low_uir_lowered_total=$low_uir_lowered_total"
  echo "proof_checked_funcs_total=$proof_checked_funcs_total"
  echo "proof_skipped_funcs_total=$proof_skipped_funcs_total"
  echo "proof_required_total=$proof_required_total"
  echo "proof_backed_changes_total=$proof_backed_changes_total"
  echo "mem2reg_loads_total=$mem2reg_loads_total"
  echo "forward_loads_total=$forward_loads_total"
  echo "changed_funcs_total=$changed_funcs_total"
  echo "unknown_slot_clobbers_total=$unknown_slot_clobbers_total"
  echo "unknown_global_clobbers_total=$unknown_global_clobbers_total"
  echo "kill_events_total=$kill_events_total"
} >"$report"

{
  echo "backend_noalias_opt_status=ok"
  echo "backend_noalias_opt_mode=runtime"
  echo "backend_noalias_opt_phase_surface=compile_stamp+generics_report+noalias_report"
  echo "backend_noalias_opt_driver=$driver"
  echo "backend_noalias_opt_phase_driver=$phase_driver"
  echo "backend_noalias_opt_phase_driver_surface=$phase_driver_surface"
  echo "backend_noalias_opt_driver_exec=$driver_exec"
  echo "backend_noalias_opt_target=$target"
  echo "backend_noalias_opt_min_probe_count=$min_probe_count"
  echo "backend_noalias_opt_probe_count=$probe_count"
  echo "backend_noalias_opt_high_uir_checked_total=$high_uir_checked_total"
  echo "backend_noalias_opt_high_uir_fallback_total=$high_uir_fallback_total"
  echo "backend_noalias_opt_low_uir_lowered_total=$low_uir_lowered_total"
  echo "backend_noalias_opt_proof_checked_funcs_total=$proof_checked_funcs_total"
  echo "backend_noalias_opt_proof_skipped_funcs_total=$proof_skipped_funcs_total"
  echo "backend_noalias_opt_proof_required_total=$proof_required_total"
  echo "backend_noalias_opt_proof_backed_changes_total=$proof_backed_changes_total"
  echo "backend_noalias_opt_mem2reg_loads_total=$mem2reg_loads_total"
  echo "backend_noalias_opt_forward_loads_total=$forward_loads_total"
  echo "backend_noalias_opt_changed_funcs_total=$changed_funcs_total"
  echo "backend_noalias_opt_unknown_slot_clobbers_total=$unknown_slot_clobbers_total"
  echo "backend_noalias_opt_unknown_global_clobbers_total=$unknown_global_clobbers_total"
  echo "backend_noalias_opt_kill_events_total=$kill_events_total"
  echo "backend_noalias_opt_report=$report"
} >"$snapshot"

echo "verify_backend_noalias_opt ok"
