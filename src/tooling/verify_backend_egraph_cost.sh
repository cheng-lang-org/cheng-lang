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
  echo "[verify_backend_egraph_cost] rg is required" 1>&2
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
  echo "[verify_backend_egraph_cost] backend driver not executable: $driver" 1>&2
  exit 1
fi
refresh_missing_marker="${PAR05_DRIVER_REFRESH:-0}"
case "$refresh_missing_marker" in
  0|1) ;;
  *) refresh_missing_marker="0" ;;
esac
driver_marker_egraph="uir_opt2.egraph"
driver_marker_weights="uir_opt2.cost_model.weights"
if ! driver_has_marker "$driver" "$driver_marker_egraph" || ! driver_has_marker "$driver" "$driver_marker_weights"; then
  if [ "$refresh_missing_marker" = "1" ]; then
    refreshed_driver="$(env \
      BACKEND_DRIVER_PATH_PREFER_REBUILD=1 \
      BACKEND_DRIVER_ALLOW_FALLBACK=0 \
      sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
    if [ -x "$refreshed_driver" ] \
      && driver_has_marker "$refreshed_driver" "$driver_marker_egraph" \
      && driver_has_marker "$refreshed_driver" "$driver_marker_weights"; then
      echo "[verify_backend_egraph_cost] use refreshed backend driver: $refreshed_driver" 1>&2
      driver="$refreshed_driver"
    fi
  fi
fi
if ! driver_has_marker "$driver" "$driver_marker_egraph"; then
  echo "[verify_backend_egraph_cost] backend driver missing egraph marker (${driver_marker_egraph}): $driver" 1>&2
  exit 1
fi
if ! driver_has_marker "$driver" "$driver_marker_weights"; then
  echo "[verify_backend_egraph_cost] backend driver missing weight marker (${driver_marker_weights}): $driver" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
fixture="${BACKEND_EGRAPH_FIXTURE:-tests/cheng/backend/fixtures/return_egraph_goal_rewrite.cheng}"
iters="${UIR_EGRAPH_ITERS:-3}"
goals_raw="${UIR_EGRAPH_GOALS:-balanced,latency,size}"
weight_expect_balanced="${UIR_EGRAPH_WEIGHT_EXPECT_BALANCED:-balanced.n4_o3_i1}"
weight_expect_latency="${UIR_EGRAPH_WEIGHT_EXPECT_LATENCY:-latency.n3_o5_i1}"
weight_expect_size="${UIR_EGRAPH_WEIGHT_EXPECT_SIZE:-size.n8_o2_i1}"
if [ ! -f "$fixture" ]; then
  echo "[verify_backend_egraph_cost] missing fixture: $fixture" 1>&2
  exit 2
fi

is_uint() {
  case "$1" in
    ''|*[!0-9]*) return 1 ;;
  esac
  return 0
}
if ! is_uint "$iters" || [ "$iters" -le 0 ] || [ "$iters" -gt 32 ]; then
  echo "[verify_backend_egraph_cost] invalid UIR_EGRAPH_ITERS: $iters (expected 1..32)" 1>&2
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

goal_weight_signature() {
  case "$1" in
    balanced) printf '%s\n' "$weight_expect_balanced" ;;
    latency) printf '%s\n' "$weight_expect_latency" ;;
    size) printf '%s\n' "$weight_expect_size" ;;
    *) printf '%s\n' "$weight_expect_balanced" ;;
  esac
}

goals=""
for raw_goal in $(printf '%s' "$goals_raw" | tr ',;' '  '); do
  goal="$(normalize_goal "$raw_goal")"
  case " $goals " in
    *" $goal "*) ;;
    *) goals="$goals $goal" ;;
  esac
done
if [ "$goals" = "" ]; then
  goals=" balanced latency size"
fi

for required in \
  "src/backend/uir/uir_egraph_cost.cheng" \
  "src/backend/uir/uir_egraph_rewrite.cheng" \
  "src/backend/uir/uir_opt.cheng"; do
  if [ ! -f "$required" ]; then
    echo "[verify_backend_egraph_cost] missing file: $required" 1>&2
    exit 2
  fi
done

require_marker() {
  file="$1"
  pat="$2"
  label="$3"
  if ! rg -q "$pat" "$file"; then
    echo "[verify_backend_egraph_cost] missing marker ($label) in $file" 1>&2
    exit 1
  fi
}

require_marker "src/backend/uir/uir_egraph_cost.cheng" "fn uirEGraphScore\\(" "egraph_cost_score"
require_marker "src/backend/uir/uir_egraph_cost.cheng" "fn uirEGraphCostAccept\\(" "egraph_cost_accept"
require_marker "src/backend/uir/uir_egraph_cost.cheng" "fn uirEGraphNodeWeight\\(" "egraph_cost_node_weight"
require_marker "src/backend/uir/uir_egraph_cost.cheng" "fn uirEGraphOpWeight\\(" "egraph_cost_op_weight"
require_marker "src/backend/uir/uir_egraph_cost.cheng" "fn uirEGraphGoalWeightSignature\\(" "egraph_cost_weight_signature"
require_marker "src/backend/uir/uir_egraph_rewrite.cheng" "fn uirRunEGraphRewrite\\(" "egraph_rewrite_entry"
require_marker "src/backend/uir/uir_egraph_rewrite.cheng" "fn uirTryFoldConstIntAlgebra\\(" "egraph_rewrite_const_int_fold"
require_marker "src/backend/uir/uir_egraph_rewrite.cheng" "fn uirTryRewriteIntAlgebra\\(" "egraph_rewrite_int_algebra_subset"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.egraph" "egraph_profile_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.egraph\\.proof_required" "egraph_proof_required_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.egraph\\.proof_backed_funcs=" "egraph_proof_backed_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.cost_model" "cost_model_profile_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.cost_model\\.weights" "cost_model_weight_profile_surface"

out_dir="artifacts/backend_egraph_cost"
mkdir -p "$out_dir"
report="$out_dir/egraph_cost.report.txt"
snapshot="$out_dir/egraph_cost.snapshot.env"
per_goal="$out_dir/egraph_cost.per_goal.tsv"
rm -f "$report" "$snapshot" "$per_goal"
printf "goal\tobj_a\tobj_b\tsize_a\tsize_b\ttotal_ms_a\ttotal_ms_b\tegraph_changed_hits\tproof_backed_funcs\tweight_signature\tweight_event_hits\n" >"$per_goal"

build_one() {
  out_obj="$1"
  out_log="$2"
  goal="$3"
  set +e
  env \
    ABI=v2_noptr \
    STAGE1_STD_NO_POINTERS=0 \
    STAGE1_STD_NO_POINTERS_STRICT=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_OWNERSHIP=0 \
    UIR_PROFILE=1 \
    UIR_NOALIAS=1 \
    UIR_NOALIAS_REQUIRE_PROOF=1 \
    UIR_EGRAPH_ITERS="$iters" \
    UIR_EGRAPH_GOAL="$goal" \
    UIR_EGRAPH_REQUIRE_PROOF=1 \
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

profile_event_hits() {
  file="$1"
  event="$2"
  awk -v target_event="$event" '
    $1 == "uir_profile" && $2 == target_event {
      n = n + 1
    }
    END {
      print n + 0
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

goals_verified=0
goals_changed=0
goals_weight_verified=0
for goal in $goals; do
  obj_a="$out_dir/egraph_cost.${goal}.a.o"
  obj_b="$out_dir/egraph_cost.${goal}.b.o"
  log_a="$out_dir/egraph_cost.${goal}.a.log"
  log_b="$out_dir/egraph_cost.${goal}.b.log"
  rm -f "$obj_a" "$obj_b" "$log_a" "$log_b"

  if ! build_one "$obj_a" "$log_a" "$goal"; then
    echo "[verify_backend_egraph_cost] compile failed (goal=$goal run=a): $fixture" 1>&2
    tail -n 200 "$log_a" 1>&2 || true
    exit 1
  fi
  if ! build_one "$obj_b" "$log_b" "$goal"; then
    echo "[verify_backend_egraph_cost] compile failed (goal=$goal run=b): $fixture" 1>&2
    tail -n 200 "$log_b" 1>&2 || true
    exit 1
  fi
  if [ ! -s "$obj_a" ] || [ ! -s "$obj_b" ]; then
    echo "[verify_backend_egraph_cost] missing object outputs for goal=$goal" 1>&2
    exit 1
  fi
  if ! cmp -s "$obj_a" "$obj_b"; then
    echo "[verify_backend_egraph_cost] deterministic object mismatch for goal=$goal: $obj_a vs $obj_b" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph([[:space:]]+|$)' "$log_a"; then
    echo "[verify_backend_egraph_cost] missing egraph profile event for goal=$goal: $log_a" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph\.changed([[:space:]]+|$)' "$log_a"; then
    echo "[verify_backend_egraph_cost] missing egraph changed event for goal=$goal: $log_a" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.egraph\.proof_required([[:space:]]+|$)' "$log_a"; then
    echo "[verify_backend_egraph_cost] missing egraph proof-required profile event for goal=$goal: $log_a" 1>&2
    exit 1
  fi
  proof_backed_funcs="$(profile_label_int_value "$log_a" "uir_opt2.egraph.proof_backed_funcs=")"
  if ! is_uint "${proof_backed_funcs:-}" || [ "${proof_backed_funcs:-0}" -le 0 ]; then
    echo "[verify_backend_egraph_cost] invalid proof-backed funcs for goal=$goal: ${proof_backed_funcs:-<empty>} ($log_a)" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.cost_model([[:space:]]+|$)' "$log_a"; then
    echo "[verify_backend_egraph_cost] missing cost model profile event for goal=$goal: $log_a" 1>&2
    exit 1
  fi
  if ! rg -q "^uir_profile[[:space:]]+uir_opt2\\.cost_model\\.${goal}([[:space:]]+|$)" "$log_a"; then
    echo "[verify_backend_egraph_cost] missing goal cost profile event for goal=$goal: $log_a" 1>&2
    exit 1
  fi
  expected_weight_sig="$(goal_weight_signature "$goal")"
  weight_event="uir_opt2.cost_model.weights.${expected_weight_sig}"
  weight_hits="$(profile_event_hits "$log_a" "$weight_event")"
  if [ "${weight_hits:-0}" -le 0 ]; then
    echo "[verify_backend_egraph_cost] missing cost model weight profile event for goal=$goal (${weight_event}): $log_a" 1>&2
    exit 1
  fi

  size_a="$(wc -c < "$obj_a" | tr -d ' ')"
  size_b="$(wc -c < "$obj_b" | tr -d ' ')"
  total_ms_a="$(profile_metric "$log_a" "single.emit_obj" "total_ms")"
  total_ms_b="$(profile_metric "$log_b" "single.emit_obj" "total_ms")"
  changed_hits="$(profile_event_hits "$log_a" "uir_opt2.egraph.changed")"
  if [ "${changed_hits:-0}" -gt 0 ]; then
    goals_changed=$((goals_changed + 1))
  fi
  goals_weight_verified=$((goals_weight_verified + 1))
  goals_verified=$((goals_verified + 1))
  printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" \
    "$goal" "$obj_a" "$obj_b" "$size_a" "$size_b" "${total_ms_a:-na}" "${total_ms_b:-na}" "${changed_hits:-0}" "${proof_backed_funcs:-0}" "$expected_weight_sig" "${weight_hits:-0}" >>"$per_goal"
done

if [ "$goals_verified" -le 0 ]; then
  echo "[verify_backend_egraph_cost] no valid egraph goals configured: $goals_raw" 1>&2
  exit 1
fi
if [ "$goals_changed" -le 0 ]; then
  echo "[verify_backend_egraph_cost] egraph changed signal missing for all goals" 1>&2
  exit 1
fi
if [ "$goals_weight_verified" -ne "$goals_verified" ]; then
  echo "[verify_backend_egraph_cost] cost model weight profile coverage mismatch: verified=$goals_weight_verified expected=$goals_verified" 1>&2
  exit 1
fi

{
  echo "verify_backend_egraph_cost report"
  echo "driver=$driver"
  echo "target=$target"
  echo "fixture=$fixture"
  echo "egraph_iters=$iters"
  echo "weight_expect_balanced=$weight_expect_balanced"
  echo "weight_expect_latency=$weight_expect_latency"
  echo "weight_expect_size=$weight_expect_size"
  echo "proof_mode=require_high_uir"
  echo "goals=$(printf '%s' "$goals" | sed 's/^ //')"
  echo "goals_verified=$goals_verified"
  echo "goals_changed=$goals_changed"
  echo "goals_weight_verified=$goals_weight_verified"
  echo "per_goal=$per_goal"
  echo "sample_log=$out_dir/egraph_cost.balanced.a.log"
} >"$report"

{
  echo "backend_egraph_cost_driver=$driver"
  echo "backend_egraph_cost_target=$target"
  echo "backend_egraph_cost_fixture=$fixture"
  echo "backend_egraph_cost_iters=$iters"
  echo "backend_egraph_cost_weight_expect_balanced=$weight_expect_balanced"
  echo "backend_egraph_cost_weight_expect_latency=$weight_expect_latency"
  echo "backend_egraph_cost_weight_expect_size=$weight_expect_size"
  echo "backend_egraph_cost_proof_mode=require_high_uir"
  echo "backend_egraph_cost_goals=$(printf '%s' "$goals" | sed 's/^ //')"
  echo "backend_egraph_cost_goals_verified=$goals_verified"
  echo "backend_egraph_cost_goals_changed=$goals_changed"
  echo "backend_egraph_cost_goals_weight_verified=$goals_weight_verified"
  echo "backend_egraph_cost_per_goal=$per_goal"
  echo "backend_egraph_cost_report=$report"
} >"$snapshot"

echo "verify_backend_egraph_cost ok"
