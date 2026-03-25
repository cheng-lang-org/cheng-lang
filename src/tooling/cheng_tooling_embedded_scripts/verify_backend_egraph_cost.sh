#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"
. "$root/src/tooling/cheng_tooling_embedded_scripts/proof_phase_driver_common.sh"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_egraph_cost] rg is required" 1>&2
  exit 2
fi

file_sha256() {
  f="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$f" | awk '{print $1}'
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | awk '{print $1}'
    return 0
  fi
  if command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$f" | awk '{print $NF}'
    return 0
  fi
  echo ""
}

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

require_profile_event() {
  log_file="$1"
  pat="$2"
  label="$3"
  if ! rg -q "$pat" "$log_file"; then
    echo "[verify_backend_egraph_cost] missing profile event ($label): $log_file" 1>&2
    exit 1
  fi
}

out_dir="artifacts/backend_egraph_cost"
rm -rf "$out_dir"
mkdir -p "$out_dir"
report="$out_dir/verify_backend_egraph_cost.report.txt"
snapshot="$out_dir/verify_backend_egraph_cost.snapshot.env"

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
if [ ! -x "$tool" ]; then
  echo "[verify_backend_egraph_cost] missing tooling binary: $tool" 1>&2
  exit 1
fi

driver="$($tool driver-path --path-only 2>/dev/null | tail -n 1 | tr -d '\r' || true)"
if [ "$driver" = "" ]; then
  driver="artifacts/backend_driver/cheng"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_egraph_cost] backend driver not executable: $driver" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-$($tool detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
proof_phase_driver_pick \
  "verify_backend_egraph_cost" \
  "$driver" \
  "$target" \
  "$out_dir" \
  "${BACKEND_PROOF_PHASE_PREFLIGHT_FIXTURE:-tests/cheng/backend/fixtures/return_i64.cheng}"
phase_driver="$proof_phase_driver_path"
phase_driver_surface="$proof_phase_driver_surface"
driver_exec="$phase_driver"
driver_real_env="$proof_phase_driver_env"
mode="runtime"
report_mode="runtime_profile_surface"

source_ok=1
for file in \
  "src/backend/uir/uir_egraph_cost.cheng" \
  "src/backend/uir/uir_egraph_rewrite.cheng" \
  "src/backend/uir/uir_opt.cheng"; do
  if [ ! -f "$file" ]; then
    source_ok=0
  fi
done
for pat in 'uirEGraphGoalWeightSignature' 'uir_opt2\.egraph' 'UIR_EGRAPH_REQUIRE_PROOF'; do
  if ! rg -q "$pat" src/backend/uir/uir_egraph_cost.cheng src/backend/uir/uir_opt.cheng; then
    source_ok=0
  fi
done
for pat in 'fn uirEGraphApplyRule\(' 'fn uirEGraphSaturateExpr\(' 'egraph_report' 'UIR_EGRAPH_CLASS_NODE_BUDGET' 'UIR_EGRAPH_CLASS_BUDGET'; do
  if ! rg -q "$pat" src/backend/uir/uir_egraph_rewrite.cheng; then
    source_ok=0
  fi
done
if [ "$source_ok" != "1" ]; then
  echo "[verify_backend_egraph_cost] source contract markers missing" 1>&2
  exit 1
fi

fixture_list="${BACKEND_EGRAPH_FIXTURES:-tests/cheng/backend/fixtures/return_opt2_algebraic.cse.cheng;tests/cheng/backend/fixtures/return_opt2_inline_dce.cheng}"
fixture_lines="$(printf '%s\n' "$fixture_list" | tr ';' '\n' | tr '\r' '\n' | sed -e '/^[[:space:]]*$/d')"
report_count=0
extracted_total=0
candidates_total=0
fixture_count=0
determinism_mismatch=0
binary_hash_mismatch=0
noalias_report_count=0
proof_backed_funcs_total=0
high_uir_checked_total=0
high_uir_fallback_total=0
low_uir_lowered_total=0
skip_ownership_effective_nonzero_count=0
phase_contract_version_mismatch_count=0
phase_model_mismatch_count=0
max_determinism_mismatch="${BACKEND_EGRAPH_COST_MAX_DETERMINISM_MISMATCH:-0}"
case "$max_determinism_mismatch" in
  ''|*[!0-9]*) max_determinism_mismatch=0 ;;
esac
enforce_binary_hash="${BACKEND_EGRAPH_COST_ENFORCE_BINARY_HASH:-0}"
case "$enforce_binary_hash" in
  1|true|TRUE|yes|YES) enforce_binary_hash=1 ;;
  *) enforce_binary_hash=0 ;;
esac
max_binary_hash_mismatch="${BACKEND_EGRAPH_COST_MAX_BINARY_HASH_MISMATCH:-0}"
case "$max_binary_hash_mismatch" in
  ''|*[!0-9]*) max_binary_hash_mismatch=0 ;;
esac

while IFS= read -r fixture; do
  [ "$fixture" = "" ] && continue
  if [ ! -f "$fixture" ]; then
    continue
  fi
  fixture_count=$((fixture_count + 1))
  for goal in balanced latency size; do
    out_a="$out_dir/egraph_probe.${fixture_count}.${goal}.a.bin"
    out_b="$out_dir/egraph_probe.${fixture_count}.${goal}.b.bin"
    log_a="$out_dir/egraph_probe.${fixture_count}.${goal}.a.log"
    log_b="$out_dir/egraph_probe.${fixture_count}.${goal}.b.log"
    stamp_a="$out_dir/egraph_probe.${fixture_count}.${goal}.a.compile_stamp.txt"
    stamp_b="$out_dir/egraph_probe.${fixture_count}.${goal}.b.compile_stamp.txt"
    trace_a="$out_dir/egraph_probe.${fixture_count}.${goal}.a.trace.log"
    trace_b="$out_dir/egraph_probe.${fixture_count}.${goal}.b.trace.log"

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
      BACKEND_COMPILE_STAMP_OUT="$stamp_a" \
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
      BACKEND_OUTPUT="$out_a" \
      UIR_NOALIAS=1 \
      UIR_NOALIAS_REQUIRE_PROOF=1 \
      UIR_EGRAPH_ITERS=4 \
      UIR_EGRAPH_GOAL="$goal" \
      UIR_EGRAPH_REQUIRE_PROOF=1 \
      UIR_EGRAPH_REPORT=1 \
      "$driver_exec" >"$log_a" 2>&1
    rc_a="$?"
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
      BACKEND_COMPILE_STAMP_OUT="$stamp_b" \
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
      BACKEND_OUTPUT="$out_b" \
      UIR_NOALIAS=1 \
      UIR_NOALIAS_REQUIRE_PROOF=1 \
      UIR_EGRAPH_ITERS=4 \
      UIR_EGRAPH_GOAL="$goal" \
      UIR_EGRAPH_REQUIRE_PROOF=1 \
      UIR_EGRAPH_REPORT=1 \
      "$driver_exec" >"$log_b" 2>&1
    rc_b="$?"
    set -e

    if [ "$rc_a" -ne 0 ] || [ "$rc_b" -ne 0 ] || [ ! -s "$out_a" ] || [ ! -s "$out_b" ]; then
      echo "[verify_backend_egraph_cost] runtime probe compile failed fixture=$fixture goal=$goal" 1>&2
      sed -n '1,120p' "$log_a" 1>&2 || true
      sed -n '1,120p' "$log_b" 1>&2 || true
      exit 1
    fi
    if [ ! -s "$stamp_a" ] || [ ! -s "$stamp_b" ]; then
      echo "[verify_backend_egraph_cost] missing compile stamp fixture=$fixture goal=$goal" 1>&2
      exit 1
    fi

    phase_model="$(extract_stamp_field "$stamp_a" "uir_phase_model")"
    phase_version="$(extract_stamp_field "$stamp_a" "uir_phase_contract_version")"
    skip_ownership_effective="$(extract_internal_ownership_fixed_0_effective "$stamp_a")"
    high_uir_checked="$(extract_phase_field "$log_a" "high_uir_checked_funcs")"
    high_uir_fallback="$(extract_phase_field "$log_a" "high_uir_fallback_funcs")"
    low_uir_lowered="$(extract_phase_field "$log_a" "low_uir_lowered_funcs")"
    if ! rg -q '^(generics_report[[:space:]]+|\[backend\] stage1: phase_contract_done )' "$log_a"; then
      echo "[verify_backend_egraph_cost] phase contract marker missing fixture=$fixture goal=$goal" 1>&2
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
        echo "[verify_backend_egraph_cost] invalid phase metric (${key}) fixture=$fixture goal=$goal value=${val:-<empty>}" 1>&2
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

    require_profile_event "$log_a" '^uir_profile[[:space:]]+uir_opt2\.egraph\.proof_required([[:space:]]+|$)' "egraph_proof_required"
    require_profile_event "$log_a" '^uir_profile[[:space:]]+uir_opt2\.egraph\.proof_backed_funcs=' "egraph_proof_backed_funcs"
    require_profile_event "$log_a" '^uir_profile[[:space:]]+uir_opt2\.egraph([[:space:]]+|$)' "egraph_pass"
    require_profile_event "$log_a" '^uir_profile[[:space:]]+uir_opt2\.cost_model([[:space:]]+|$)' "cost_model_pass"
    require_profile_event "$log_a" "^uir_profile[[:space:]]+uir_opt2\\.cost_model\\.${goal}([[:space:]]+|$)" "cost_model_goal_${goal}"

    proof_backed_funcs="$(profile_label_int_value "$log_a" "uir_opt2.egraph.proof_backed_funcs=")"
    if ! is_uint "$proof_backed_funcs"; then
      echo "[verify_backend_egraph_cost] invalid proof_backed_funcs fixture=$fixture goal=$goal value=${proof_backed_funcs:-<empty>}" 1>&2
      exit 1
    fi
    proof_backed_funcs_total=$((proof_backed_funcs_total + proof_backed_funcs))

    noalias_report_count=$((noalias_report_count + $(rg -c '^noalias_report' "$log_a" 2>/dev/null || echo 0)))
    report_count=$((report_count + $(rg -c '^egraph_report' "$log_a" 2>/dev/null || echo 0)))
    extracted_total=$((extracted_total + $(awk -F 'extracted=' '/^egraph_report/{split($2,a,"\t"); s+=a[1]+0} END{print s+0}' "$log_a" 2>/dev/null || echo 0)))
    candidates_total=$((candidates_total + $(awk -F 'candidates=' '/^egraph_report/{split($2,a,"\t"); s+=a[1]+0} END{print s+0}' "$log_a" 2>/dev/null || echo 0)))

    hash_a="$(file_sha256 "$out_a")"
    hash_b="$(file_sha256 "$out_b")"
    if [ "$hash_a" = "" ] || [ "$hash_b" = "" ] || [ "$hash_a" != "$hash_b" ]; then
      binary_hash_mismatch=$((binary_hash_mismatch + 1))
    fi

    awk '/^(noalias_report|ssu_report|egraph_report)/{print}' "$log_a" >"$trace_a"
    awk '/^(noalias_report|ssu_report|egraph_report)/{print}' "$log_b" >"$trace_b"
    if ! cmp -s "$trace_a" "$trace_b"; then
      determinism_mismatch=$((determinism_mismatch + 1))
    fi
  done
done <<EOF
$fixture_lines
EOF

if [ "$fixture_count" -le 0 ]; then
  echo "[verify_backend_egraph_cost] missing fixture (none from list exist)" 1>&2
  exit 2
fi
if [ "$phase_model_mismatch_count" -gt 0 ]; then
  echo "[verify_backend_egraph_cost] phase model mismatch across proof probes: count=$phase_model_mismatch_count expected=single_ir_dual_phase" 1>&2
  exit 1
fi
if [ "$phase_contract_version_mismatch_count" -gt 0 ]; then
  echo "[verify_backend_egraph_cost] phase contract version mismatch across proof probes: count=$phase_contract_version_mismatch_count expected=p4_phase_v1" 1>&2
  exit 1
fi
if [ "$skip_ownership_effective_nonzero_count" -gt 0 ]; then
  echo "[verify_backend_egraph_cost] ownership proof path not engaged: stage1_ownership_fixed_0_effective!=0 count=$skip_ownership_effective_nonzero_count" 1>&2
  exit 1
fi
if [ "$high_uir_checked_total" -le 0 ]; then
  echo "[verify_backend_egraph_cost] no high_uir_checked_funcs observed across proof probes" 1>&2
  exit 1
fi
if [ "$proof_backed_funcs_total" -le 0 ]; then
  echo "[verify_backend_egraph_cost] no proof_backed_funcs observed across egraph proof probes" 1>&2
  exit 1
fi
if [ "$determinism_mismatch" -gt "$max_determinism_mismatch" ]; then
  echo "[verify_backend_egraph_cost] determinism mismatch exceeds threshold: mismatch=$determinism_mismatch max=$max_determinism_mismatch" 1>&2
  exit 1
fi
if [ "$enforce_binary_hash" = "1" ] && [ "$binary_hash_mismatch" -gt "$max_binary_hash_mismatch" ]; then
  echo "[verify_backend_egraph_cost] binary hash mismatch exceeds threshold: mismatch=$binary_hash_mismatch max=$max_binary_hash_mismatch" 1>&2
  exit 1
fi
if [ "$noalias_report_count" -le 0 ]; then
  echo "[verify_backend_egraph_cost] noalias_report missing across egraph runtime probes" 1>&2
  exit 1
fi
if [ "$report_count" -gt 0 ]; then
  report_mode="runtime_report_surface"
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
  echo "fixture_count=$fixture_count"
  echo "high_uir_checked_total=$high_uir_checked_total"
  echo "high_uir_fallback_total=$high_uir_fallback_total"
  echo "low_uir_lowered_total=$low_uir_lowered_total"
  echo "proof_backed_funcs_total=$proof_backed_funcs_total"
  echo "noalias_report_count=$noalias_report_count"
  echo "report_count=$report_count"
  echo "candidates_total=$candidates_total"
  echo "extracted_total=$extracted_total"
  echo "determinism_mismatch=$determinism_mismatch"
  echo "max_determinism_mismatch=$max_determinism_mismatch"
  echo "enforce_binary_hash=$enforce_binary_hash"
  echo "binary_hash_mismatch=$binary_hash_mismatch"
  echo "max_binary_hash_mismatch=$max_binary_hash_mismatch"
} >"$report"

{
  echo "backend_egraph_cost_status=ok"
  echo "backend_egraph_cost_mode=$mode"
  echo "backend_egraph_cost_report_mode=$report_mode"
  echo "backend_egraph_cost_phase_surface=compile_stamp+generics_report+profile"
  echo "backend_egraph_cost_driver=$driver"
  echo "backend_egraph_cost_phase_driver=$phase_driver"
  echo "backend_egraph_cost_phase_driver_surface=$phase_driver_surface"
  echo "backend_egraph_cost_driver_exec=$driver_exec"
  echo "backend_egraph_cost_fixture_count=$fixture_count"
  echo "backend_egraph_cost_high_uir_checked_total=$high_uir_checked_total"
  echo "backend_egraph_cost_high_uir_fallback_total=$high_uir_fallback_total"
  echo "backend_egraph_cost_low_uir_lowered_total=$low_uir_lowered_total"
  echo "backend_egraph_cost_proof_backed_funcs_total=$proof_backed_funcs_total"
  echo "backend_egraph_cost_noalias_report_count=$noalias_report_count"
  echo "backend_egraph_cost_report_count=$report_count"
  echo "backend_egraph_cost_candidates_total=$candidates_total"
  echo "backend_egraph_cost_extracted_total=$extracted_total"
  echo "backend_egraph_cost_determinism_mismatch=$determinism_mismatch"
  echo "backend_egraph_cost_max_determinism_mismatch=$max_determinism_mismatch"
  echo "backend_egraph_cost_enforce_binary_hash=$enforce_binary_hash"
  echo "backend_egraph_cost_binary_hash_mismatch=$binary_hash_mismatch"
  echo "backend_egraph_cost_max_binary_hash_mismatch=$max_binary_hash_mismatch"
  echo "backend_egraph_cost_report=$report"
} >"$snapshot"

echo "verify_backend_egraph_cost ok"
