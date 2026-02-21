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
  echo "[verify_backend_noalias_opt] rg is required" 1>&2
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
  echo "[verify_backend_noalias_opt] backend driver not executable: $driver" 1>&2
  exit 1
fi
refresh_missing_marker="${PAR05_DRIVER_REFRESH:-0}"
case "$refresh_missing_marker" in
  0|1) ;;
  *) refresh_missing_marker="0" ;;
esac
if ! driver_has_marker "$driver" "uir_opt2.noalias"; then
  if [ "$refresh_missing_marker" = "1" ]; then
    refreshed_driver="$(env \
      BACKEND_DRIVER_PATH_PREFER_REBUILD=1 \
      BACKEND_DRIVER_ALLOW_FALLBACK=0 \
      sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
    if [ -x "$refreshed_driver" ] && driver_has_marker "$refreshed_driver" "uir_opt2.noalias"; then
      echo "[verify_backend_noalias_opt] use refreshed backend driver: $refreshed_driver" 1>&2
      driver="$refreshed_driver"
    fi
  fi
fi
if ! driver_has_marker "$driver" "uir_opt2.noalias"; then
  echo "[verify_backend_noalias_opt] backend driver missing noalias marker: $driver" 1>&2
  exit 1
fi

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
benefit_fixture="${BACKEND_NOALIAS_BENEFIT_FIXTURE:-tests/cheng/backend/fixtures/return_opt2_sroa_deref.cheng}"
mem2reg_fixture="${BACKEND_NOALIAS_MEM2REG_FIXTURE:-tests/cheng/backend/fixtures/return_opt2_noalias_mem2reg_load.cheng}"
guard_fixture="${BACKEND_NOALIAS_GUARD_FIXTURE:-tests/cheng/backend/fixtures/return_store_deref.cheng}"
for fixture_path in "$benefit_fixture" "$mem2reg_fixture" "$guard_fixture"; do
  if [ ! -f "$fixture_path" ]; then
    echo "[verify_backend_noalias_opt] missing fixture: $fixture_path" 1>&2
    exit 2
  fi
done

for required in \
  "src/backend/uir/uir_noalias_pass.cheng" \
  "src/backend/uir/uir_opt.cheng"; do
  if [ ! -f "$required" ]; then
    echo "[verify_backend_noalias_opt] missing file: $required" 1>&2
    exit 2
  fi
done

require_marker() {
  file="$1"
  pat="$2"
  label="$3"
  if ! rg -q "$pat" "$file"; then
    echo "[verify_backend_noalias_opt] missing marker ($label) in $file" 1>&2
    exit 1
  fi
}

require_marker "src/backend/uir/uir_noalias_pass.cheng" "fn uirRunNoAliasPrep\\(" "noalias_pass_entry"
require_marker "src/backend/uir/uir_noalias_pass.cheng" "fn uirNoAliasResolveAddrBaseSlot\\(" "noalias_resolve_addr"
require_marker "src/backend/uir/uir_noalias_pass.cheng" "st\\.kind = msAssign" "noalias_mem2reg_store"
require_marker "src/backend/uir/uir_noalias_pass.cheng" "noalias_report" "noalias_report_surface"
require_marker "src/backend/uir/uir_noalias_pass.cheng" "proof_backed_changes=" "noalias_proof_backed_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.noalias" "noalias_profile_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.noalias\\.proof_required" "noalias_proof_required_profile_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt2\\.noalias\\.proof_backed_funcs=" "noalias_proof_backed_profile_surface"
require_marker "src/backend/uir/uir_opt.cheng" "uir_opt3\\.ssa\\.lower" "mem2reg_profile_surface"

out_dir="artifacts/backend_noalias_opt"
mkdir -p "$out_dir"
obj="$out_dir/noalias_opt.o"
log="$out_dir/noalias_opt.log"

benefit_obj_off="$out_dir/noalias_opt.benefit.off.o"
benefit_obj_on="$out_dir/noalias_opt.benefit.on.o"
benefit_log_off="$out_dir/noalias_opt.benefit.off.log"
benefit_log_on="$out_dir/noalias_opt.benefit.on.log"

mem2reg_obj_off="$out_dir/noalias_opt.mem2reg.off.o"
mem2reg_obj_on="$out_dir/noalias_opt.mem2reg.on.o"
mem2reg_log_off="$out_dir/noalias_opt.mem2reg.off.log"
mem2reg_log_on="$out_dir/noalias_opt.mem2reg.on.log"

guard_obj_off="$out_dir/noalias_opt.guard.off.o"
guard_obj_on="$out_dir/noalias_opt.guard.on.o"
guard_log_off="$out_dir/noalias_opt.guard.off.log"
guard_log_on="$out_dir/noalias_opt.guard.on.log"

report="$out_dir/noalias_opt.report.txt"
snapshot="$out_dir/noalias_opt.snapshot.env"
rm -f \
  "$obj" "$log" \
  "$benefit_obj_off" "$benefit_obj_on" "$benefit_log_off" "$benefit_log_on" \
  "$mem2reg_obj_off" "$mem2reg_obj_on" "$mem2reg_log_off" "$mem2reg_log_on" \
  "$guard_obj_off" "$guard_obj_on" "$guard_log_off" "$guard_log_on"

build_one() {
  fixture="$1"
  noalias="$2"
  out_obj="$3"
  out_log="$4"
  cache_dir="$out_obj.objs"
  rm -rf "$cache_dir"
  set +e
  env \
    ABI=v2_noptr \
    STAGE1_STD_NO_POINTERS=0 \
    STAGE1_STD_NO_POINTERS_STRICT=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_OWNERSHIP=0 \
    BACKEND_MODULE_CACHE= \
    BACKEND_MULTI_MODULE_CACHE= \
    UIR_PROFILE=1 \
    UIR_NOALIAS="$noalias" \
    UIR_NOALIAS_REQUIRE_PROOF=1 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_OPT_LEVEL=2 \
    BACKEND_EMIT=obj \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$out_obj" \
    "$driver" >"$out_log" 2>&1
  status="$?"
  set -e
  return "$status"
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
      if (!found) {
        print ""
      }
    }
  ' "$file"
}

is_uint() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

require_profile_events() {
  label="$1"
  off_log="$2"
  on_log="$3"
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias\.disabled([[:space:]]+|$)' "$off_log"; then
    echo "[verify_backend_noalias_opt] missing noalias disabled profile event (${label}): $off_log" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias([[:space:]]+|$)' "$on_log"; then
    echo "[verify_backend_noalias_opt] missing noalias profile event (${label}): $on_log" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.safe\.copy_prop([[:space:]]+|$)' "$on_log"; then
    echo "[verify_backend_noalias_opt] missing opt2 copy_prop event (${label}): $on_log" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias\.proof_required([[:space:]]+|$)' "$on_log"; then
    echo "[verify_backend_noalias_opt] missing noalias proof-required profile event (${label}): $on_log" 1>&2
    exit 1
  fi
  if ! rg -q '^uir_profile[[:space:]]+uir_opt2\.noalias\.proof_backed_funcs=' "$on_log"; then
    echo "[verify_backend_noalias_opt] missing noalias proof-backed funcs profile event (${label}): $on_log" 1>&2
    exit 1
  fi
}

extract_report_metric() {
  line="$1"
  key="$2"
  printf '%s\n' "$line" | tr '\t' '\n' | awk -F= -v target_key="$key" '$1==target_key{print $2; exit}'
}

parse_noalias_report() {
  log_file="$1"
  report_label="$2"
  line="$(rg '^noalias_report[[:space:]]+' "$log_file" | tail -n 1 || true)"
  if [ "$line" = "" ]; then
    echo "source_contract_fallback||0|0|0|0|0|0|0|0|0|0"
    return 0
  fi

  forwarded="$(extract_report_metric "$line" "forward_loads")"
  mem2reg_loads="$(extract_report_metric "$line" "mem2reg_loads")"
  mem2reg_stores="$(extract_report_metric "$line" "mem2reg_stores")"
  changed_funcs="$(extract_report_metric "$line" "changed_funcs")"
  proof_backed_changes="$(extract_report_metric "$line" "proof_backed_changes")"
  proof_checked_funcs="$(extract_report_metric "$line" "proof_checked_funcs")"
  proof_skipped_funcs="$(extract_report_metric "$line" "proof_skipped_funcs")"
  proof_required="$(extract_report_metric "$line" "proof_required")"
  for metric_pair in \
    "forward_loads:$forwarded" \
    "mem2reg_loads:$mem2reg_loads" \
    "mem2reg_stores:$mem2reg_stores" \
    "changed_funcs:$changed_funcs" \
    "proof_backed_changes:$proof_backed_changes" \
    "proof_checked_funcs:$proof_checked_funcs" \
    "proof_skipped_funcs:$proof_skipped_funcs" \
    "proof_required:$proof_required"; do
    metric_name="${metric_pair%%:*}"
    metric_value="${metric_pair#*:}"
    if ! is_uint "$metric_value"; then
      echo "[verify_backend_noalias_opt] invalid noalias_report metric (${report_label}/${metric_name}): ${metric_value}" 1>&2
      exit 1
    fi
  done
  total_changes=$((forwarded + mem2reg_loads + mem2reg_stores))
  echo "runtime_v2|$line|$forwarded|$mem2reg_loads|$mem2reg_stores|$changed_funcs|$proof_backed_changes|$proof_checked_funcs|$proof_skipped_funcs|$proof_required|$total_changes"
}

if ! build_one "$benefit_fixture" 0 "$benefit_obj_off" "$benefit_log_off"; then
  echo "[verify_backend_noalias_opt] compile failed (benefit/noalias=0): $benefit_fixture" 1>&2
  tail -n 200 "$benefit_log_off" 1>&2 || true
  exit 1
fi
if ! build_one "$benefit_fixture" 1 "$benefit_obj_on" "$benefit_log_on"; then
  echo "[verify_backend_noalias_opt] compile failed (benefit/noalias=1): $benefit_fixture" 1>&2
  tail -n 200 "$benefit_log_on" 1>&2 || true
  exit 1
fi

if ! build_one "$mem2reg_fixture" 0 "$mem2reg_obj_off" "$mem2reg_log_off"; then
  echo "[verify_backend_noalias_opt] compile failed (mem2reg/noalias=0): $mem2reg_fixture" 1>&2
  tail -n 200 "$mem2reg_log_off" 1>&2 || true
  exit 1
fi
if ! build_one "$mem2reg_fixture" 1 "$mem2reg_obj_on" "$mem2reg_log_on"; then
  echo "[verify_backend_noalias_opt] compile failed (mem2reg/noalias=1): $mem2reg_fixture" 1>&2
  tail -n 200 "$mem2reg_log_on" 1>&2 || true
  exit 1
fi

if ! build_one "$guard_fixture" 0 "$guard_obj_off" "$guard_log_off"; then
  echo "[verify_backend_noalias_opt] compile failed (guard/noalias=0): $guard_fixture" 1>&2
  tail -n 200 "$guard_log_off" 1>&2 || true
  exit 1
fi
if ! build_one "$guard_fixture" 1 "$guard_obj_on" "$guard_log_on"; then
  echo "[verify_backend_noalias_opt] compile failed (guard/noalias=1): $guard_fixture" 1>&2
  tail -n 200 "$guard_log_on" 1>&2 || true
  exit 1
fi

for out_obj in \
  "$benefit_obj_off" "$benefit_obj_on" \
  "$mem2reg_obj_off" "$mem2reg_obj_on" \
  "$guard_obj_off" "$guard_obj_on"; do
  if [ ! -s "$out_obj" ]; then
    echo "[verify_backend_noalias_opt] missing object output: $out_obj" 1>&2
    exit 1
  fi
done

require_profile_events "benefit" "$benefit_log_off" "$benefit_log_on"
require_profile_events "mem2reg" "$mem2reg_log_off" "$mem2reg_log_on"
require_profile_events "guard" "$guard_log_off" "$guard_log_on"

benefit_report="$(parse_noalias_report "$benefit_log_on" "benefit")"
IFS='|' read -r benefit_mode benefit_report_line benefit_forwarded benefit_mem2reg_loads benefit_mem2reg_stores benefit_changed_funcs benefit_proof_backed_changes benefit_proof_checked_funcs benefit_proof_skipped_funcs benefit_proof_required benefit_total_changes <<EOF
$benefit_report
EOF
mem2reg_report="$(parse_noalias_report "$mem2reg_log_on" "mem2reg")"
IFS='|' read -r mem2reg_mode mem2reg_report_line mem2reg_forwarded mem2reg_mem2reg_loads mem2reg_mem2reg_stores mem2reg_changed_funcs mem2reg_proof_backed_changes mem2reg_proof_checked_funcs mem2reg_proof_skipped_funcs mem2reg_proof_required mem2reg_total_changes <<EOF
$mem2reg_report
EOF
guard_report="$(parse_noalias_report "$guard_log_on" "guard")"
IFS='|' read -r guard_mode guard_report_line guard_forwarded guard_mem2reg_loads guard_mem2reg_stores guard_changed_funcs guard_proof_backed_changes guard_proof_checked_funcs guard_proof_skipped_funcs guard_proof_required guard_total_changes <<EOF
$guard_report
EOF

if [ "$benefit_mode" = "runtime_v2" ]; then
  if [ "$benefit_forwarded" -le 0 ] || [ "$benefit_mem2reg_stores" -le 0 ] || [ "$benefit_changed_funcs" -le 0 ]; then
    echo "[verify_backend_noalias_opt] benefit fixture missing forward/mem2reg_store signal: $benefit_report_line" 1>&2
    exit 1
  fi
  if [ "$benefit_proof_backed_changes" -le 0 ] || [ "$benefit_proof_checked_funcs" -le 0 ] || [ "$benefit_proof_required" -ne 1 ]; then
    echo "[verify_backend_noalias_opt] benefit fixture missing proof-backed noalias signal: $benefit_report_line" 1>&2
    exit 1
  fi
fi
if [ "$mem2reg_mode" = "runtime_v2" ]; then
  if [ "$mem2reg_mem2reg_loads" -le 0 ] || [ "$mem2reg_changed_funcs" -le 0 ]; then
    echo "[verify_backend_noalias_opt] mem2reg fixture missing mem2reg_load signal: $mem2reg_report_line" 1>&2
    exit 1
  fi
  if [ "$mem2reg_proof_backed_changes" -le 0 ] || [ "$mem2reg_proof_checked_funcs" -le 0 ] || [ "$mem2reg_proof_required" -ne 1 ]; then
    echo "[verify_backend_noalias_opt] mem2reg fixture missing proof-backed noalias signal: $mem2reg_report_line" 1>&2
    exit 1
  fi
fi
if ! cmp -s "$guard_obj_off" "$guard_obj_on"; then
  echo "[verify_backend_noalias_opt] guard object mismatch (expected fallback-stable): $guard_obj_off vs $guard_obj_on" 1>&2
  exit 1
fi
if [ "$guard_mode" = "runtime_v2" ]; then
  if [ "$guard_total_changes" -ne 0 ] || [ "$guard_changed_funcs" -ne 0 ]; then
    echo "[verify_backend_noalias_opt] guard fixture expected zero noalias changes: $guard_report_line" 1>&2
    exit 1
  fi
  if [ "$guard_proof_required" -ne 1 ]; then
    echo "[verify_backend_noalias_opt] guard fixture missing proof-required noalias mode: $guard_report_line" 1>&2
    exit 1
  fi
fi

noalias_report_mode="runtime_v2"
for mode in "$benefit_mode" "$mem2reg_mode" "$guard_mode"; do
  if [ "$mode" != "runtime_v2" ]; then
    noalias_report_mode="source_contract_fallback"
    break
  fi
done

cp "$benefit_obj_on" "$obj"
cp "$benefit_log_on" "$log"

size_off="$(wc -c < "$benefit_obj_off" | tr -d ' ')"
size_on="$(wc -c < "$benefit_obj_on" | tr -d ' ')"
size_delta="$((size_off - size_on))"

mem2reg_size_off="$(wc -c < "$mem2reg_obj_off" | tr -d ' ')"
mem2reg_size_on="$(wc -c < "$mem2reg_obj_on" | tr -d ' ')"
mem2reg_size_delta="$((mem2reg_size_off - mem2reg_size_on))"

guard_size_off="$(wc -c < "$guard_obj_off" | tr -d ' ')"
guard_size_on="$(wc -c < "$guard_obj_on" | tr -d ' ')"
guard_size_delta="$((guard_size_off - guard_size_on))"

emit_total_ms_off="$(profile_metric "$benefit_log_off" "single.emit_obj" "total_ms")"
emit_total_ms_on="$(profile_metric "$benefit_log_on" "single.emit_obj" "total_ms")"
noalias_step_ms_on="$(profile_metric "$benefit_log_on" "uir_opt2.noalias" "step_ms")"
copy_prop_step_ms_on="$(profile_metric "$benefit_log_on" "uir_opt2.safe.copy_prop" "step_ms")"

benefit_hint="pass_activated_codegen_stable"
if [ "$size_delta" -gt 0 ]; then
  benefit_hint="obj_size_reduced"
elif [ "$size_delta" -lt 0 ]; then
  benefit_hint="obj_size_increased"
fi
mem2reg_hint="mem2reg_load_triggered"
if [ "$mem2reg_mem2reg_loads" -le 0 ]; then
  mem2reg_hint="mem2reg_load_missing"
fi
guard_hint="fallback_stable_guard"
if [ "$guard_size_delta" -ne 0 ]; then
  guard_hint="guard_size_changed"
fi

{
  echo "verify_backend_noalias_opt report"
  echo "driver=$driver"
  echo "target=$target"
  echo "fixture=$benefit_fixture"
  echo "object=$obj"
  echo "log=$log"
  echo "object_off=$benefit_obj_off"
  echo "object_on=$benefit_obj_on"
  echo "log_off=$benefit_log_off"
  echo "log_on=$benefit_log_on"
  echo "emit_total_ms_off=${emit_total_ms_off:-na}"
  echo "emit_total_ms_on=${emit_total_ms_on:-na}"
  echo "noalias_step_ms_on=${noalias_step_ms_on:-na}"
  echo "copy_prop_step_ms_on=${copy_prop_step_ms_on:-na}"
  echo "noalias_report_mode=$noalias_report_mode"
  echo "noalias_report=$benefit_report_line"
  echo "noalias_forwarded_loads=$benefit_forwarded"
  echo "noalias_mem2reg_loads=$benefit_mem2reg_loads"
  echo "noalias_mem2reg_stores=$benefit_mem2reg_stores"
  echo "noalias_changed_funcs=$benefit_changed_funcs"
  echo "noalias_proof_backed_changes=$benefit_proof_backed_changes"
  echo "noalias_proof_checked_funcs=$benefit_proof_checked_funcs"
  echo "noalias_proof_skipped_funcs=$benefit_proof_skipped_funcs"
  echo "noalias_proof_required=$benefit_proof_required"
  echo "obj_size_off=$size_off"
  echo "obj_size_on=$size_on"
  echo "obj_size_delta=$size_delta"
  echo "benefit_hint=$benefit_hint"
  echo "benefit_fixture=$benefit_fixture"
  echo "benefit_report_mode=$benefit_mode"
  echo "benefit_report=$benefit_report_line"
  echo "benefit_total_changes=$benefit_total_changes"
  echo "mem2reg_fixture=$mem2reg_fixture"
  echo "mem2reg_object_off=$mem2reg_obj_off"
  echo "mem2reg_object_on=$mem2reg_obj_on"
  echo "mem2reg_log_off=$mem2reg_log_off"
  echo "mem2reg_log_on=$mem2reg_log_on"
  echo "mem2reg_report_mode=$mem2reg_mode"
  echo "mem2reg_report=$mem2reg_report_line"
  echo "mem2reg_forwarded_loads=$mem2reg_forwarded"
  echo "mem2reg_mem2reg_loads=$mem2reg_mem2reg_loads"
  echo "mem2reg_mem2reg_stores=$mem2reg_mem2reg_stores"
  echo "mem2reg_changed_funcs=$mem2reg_changed_funcs"
  echo "mem2reg_proof_backed_changes=$mem2reg_proof_backed_changes"
  echo "mem2reg_proof_checked_funcs=$mem2reg_proof_checked_funcs"
  echo "mem2reg_proof_skipped_funcs=$mem2reg_proof_skipped_funcs"
  echo "mem2reg_proof_required=$mem2reg_proof_required"
  echo "mem2reg_total_changes=$mem2reg_total_changes"
  echo "mem2reg_obj_size_off=$mem2reg_size_off"
  echo "mem2reg_obj_size_on=$mem2reg_size_on"
  echo "mem2reg_obj_size_delta=$mem2reg_size_delta"
  echo "mem2reg_hint=$mem2reg_hint"
  echo "guard_fixture=$guard_fixture"
  echo "guard_object_off=$guard_obj_off"
  echo "guard_object_on=$guard_obj_on"
  echo "guard_log_off=$guard_log_off"
  echo "guard_log_on=$guard_log_on"
  echo "guard_report_mode=$guard_mode"
  echo "guard_report=$guard_report_line"
  echo "guard_forwarded_loads=$guard_forwarded"
  echo "guard_mem2reg_loads=$guard_mem2reg_loads"
  echo "guard_mem2reg_stores=$guard_mem2reg_stores"
  echo "guard_changed_funcs=$guard_changed_funcs"
  echo "guard_proof_backed_changes=$guard_proof_backed_changes"
  echo "guard_proof_checked_funcs=$guard_proof_checked_funcs"
  echo "guard_proof_skipped_funcs=$guard_proof_skipped_funcs"
  echo "guard_proof_required=$guard_proof_required"
  echo "guard_total_changes=$guard_total_changes"
  echo "guard_obj_size_off=$guard_size_off"
  echo "guard_obj_size_on=$guard_size_on"
  echo "guard_obj_size_delta=$guard_size_delta"
  echo "guard_hint=$guard_hint"
} >"$report"

{
  echo "backend_noalias_opt_driver=$driver"
  echo "backend_noalias_opt_target=$target"
  echo "backend_noalias_opt_fixture=$benefit_fixture"
  echo "backend_noalias_opt_report=$report"
  echo "backend_noalias_opt_emit_total_ms_off=${emit_total_ms_off:-}"
  echo "backend_noalias_opt_emit_total_ms_on=${emit_total_ms_on:-}"
  echo "backend_noalias_opt_noalias_step_ms_on=${noalias_step_ms_on:-}"
  echo "backend_noalias_opt_copy_prop_step_ms_on=${copy_prop_step_ms_on:-}"
  echo "backend_noalias_opt_report_mode=$noalias_report_mode"
  echo "backend_noalias_opt_forwarded_loads=$benefit_forwarded"
  echo "backend_noalias_opt_mem2reg_loads=$benefit_mem2reg_loads"
  echo "backend_noalias_opt_mem2reg_stores=$benefit_mem2reg_stores"
  echo "backend_noalias_opt_changed_funcs=$benefit_changed_funcs"
  echo "backend_noalias_opt_proof_backed_changes=$benefit_proof_backed_changes"
  echo "backend_noalias_opt_proof_checked_funcs=$benefit_proof_checked_funcs"
  echo "backend_noalias_opt_proof_skipped_funcs=$benefit_proof_skipped_funcs"
  echo "backend_noalias_opt_proof_required=$benefit_proof_required"
  echo "backend_noalias_opt_obj_size_off=$size_off"
  echo "backend_noalias_opt_obj_size_on=$size_on"
  echo "backend_noalias_opt_obj_size_delta=$size_delta"
  echo "backend_noalias_opt_benefit_hint=$benefit_hint"
  echo "backend_noalias_opt_benefit_total_changes=$benefit_total_changes"
  echo "backend_noalias_opt_mem2reg_fixture=$mem2reg_fixture"
  echo "backend_noalias_opt_mem2reg_report_mode=$mem2reg_mode"
  echo "backend_noalias_opt_mem2reg_forwarded_loads=$mem2reg_forwarded"
  echo "backend_noalias_opt_mem2reg_mem2reg_loads=$mem2reg_mem2reg_loads"
  echo "backend_noalias_opt_mem2reg_mem2reg_stores=$mem2reg_mem2reg_stores"
  echo "backend_noalias_opt_mem2reg_changed_funcs=$mem2reg_changed_funcs"
  echo "backend_noalias_opt_mem2reg_proof_backed_changes=$mem2reg_proof_backed_changes"
  echo "backend_noalias_opt_mem2reg_proof_checked_funcs=$mem2reg_proof_checked_funcs"
  echo "backend_noalias_opt_mem2reg_proof_skipped_funcs=$mem2reg_proof_skipped_funcs"
  echo "backend_noalias_opt_mem2reg_proof_required=$mem2reg_proof_required"
  echo "backend_noalias_opt_mem2reg_total_changes=$mem2reg_total_changes"
  echo "backend_noalias_opt_mem2reg_obj_size_off=$mem2reg_size_off"
  echo "backend_noalias_opt_mem2reg_obj_size_on=$mem2reg_size_on"
  echo "backend_noalias_opt_mem2reg_obj_size_delta=$mem2reg_size_delta"
  echo "backend_noalias_opt_mem2reg_hint=$mem2reg_hint"
  echo "backend_noalias_opt_guard_fixture=$guard_fixture"
  echo "backend_noalias_opt_guard_report_mode=$guard_mode"
  echo "backend_noalias_opt_guard_forwarded_loads=$guard_forwarded"
  echo "backend_noalias_opt_guard_mem2reg_loads=$guard_mem2reg_loads"
  echo "backend_noalias_opt_guard_mem2reg_stores=$guard_mem2reg_stores"
  echo "backend_noalias_opt_guard_changed_funcs=$guard_changed_funcs"
  echo "backend_noalias_opt_guard_total_changes=$guard_total_changes"
  echo "backend_noalias_opt_guard_proof_backed_changes=$guard_proof_backed_changes"
  echo "backend_noalias_opt_guard_proof_checked_funcs=$guard_proof_checked_funcs"
  echo "backend_noalias_opt_guard_proof_skipped_funcs=$guard_proof_skipped_funcs"
  echo "backend_noalias_opt_guard_proof_required=$guard_proof_required"
  echo "backend_noalias_opt_guard_obj_size_off=$guard_size_off"
  echo "backend_noalias_opt_guard_obj_size_on=$guard_size_on"
  echo "backend_noalias_opt_guard_obj_size_delta=$guard_size_delta"
  echo "backend_noalias_opt_guard_hint=$guard_hint"
} >"$snapshot"

echo "verify_backend_noalias_opt ok"
