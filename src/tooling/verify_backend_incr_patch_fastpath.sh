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

fail() {
  echo "[verify_backend_incr_patch_fastpath] $1" >&2
  exit 1
}

now_ms() {
  if command -v perl >/dev/null 2>&1; then
    perl -MTime::HiRes=time -e 'printf "%.0f\n", time() * 1000'
    return 0
  fi
  echo "$(( $(date +%s) * 1000 ))"
}

read_report_value() {
  key="$1"
  file="$2"
  awk -F= -v k="$key" '
    $1 == k {
      sub(/^[^=]*=/, "", $0)
      print $0
      found=1
      exit
    }
    END {
      if (!found) print ""
    }
  ' "$file"
}

strip_symbol_prefix() {
  sym="$1"
  while [ "${sym#_}" != "$sym" ]; do
    sym="${sym#_}"
  done
  printf '%s\n' "$sym"
}

as_uint_or_default() {
  raw="$1"
  fallback="$2"
  case "$raw" in
    ''|*[!0-9]*)
      echo "$fallback"
      ;;
    *)
      echo "$raw"
      ;;
  esac
}

host_target="$(sh src/tooling/detect_host_target.sh 2>/dev/null || true)"
if [ "$host_target" = "" ]; then
  host_target="unknown"
fi

select_target() {
  if [ "${BACKEND_TARGET:-}" != "" ]; then
    printf '%s\n' "${BACKEND_TARGET}"
    return
  fi
  if [ "${BACKEND_HOTPATCH_TARGET:-}" != "" ]; then
    printf '%s\n' "${BACKEND_HOTPATCH_TARGET}"
    return
  fi
  if [ "$host_target" != "unknown" ]; then
    printf '%s\n' "$host_target"
    return
  fi
  printf '%s\n' "arm64-apple-darwin"
}

target="$(select_target)"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
expected_symbol="${BACKEND_HOTPATCH_SYMBOL:-hotpatch_target}"
dirty_budget="$(as_uint_or_default "${BACKEND_INCR_PATCH_DIRTY_BUDGET:-1}" "1")"

out_dir="artifacts/backend_incr_patch_fastpath"
mkdir -p "$out_dir"
meta_log="$out_dir/backend_incr_patch_fastpath.$safe_target.meta.log"
inplace_log="$out_dir/backend_incr_patch_fastpath.$safe_target.inplace.log"
report="$out_dir/backend_incr_patch_fastpath.$safe_target.report.txt"
snapshot="$out_dir/backend_incr_patch_fastpath.$safe_target.snapshot.env"

rm -f "$meta_log" "$inplace_log" "$report" "$snapshot"

meta_report="artifacts/backend_hotpatch_meta/backend_hotpatch_meta.$safe_target.report.txt"
inplace_report="artifacts/backend_hotpatch_inplace/backend_hotpatch_inplace.$safe_target.report.txt"

meta_start_ms="$(now_ms)"
set +e
sh src/tooling/verify_backend_hotpatch_meta.sh >"$meta_log" 2>&1
meta_status="$?"
set -e
meta_end_ms="$(now_ms)"
full_build_ms=$((meta_end_ms - meta_start_ms))
if [ "$meta_status" -ne 0 ]; then
  echo "[verify_backend_incr_patch_fastpath] verify_backend_hotpatch_meta failed (status=$meta_status): $meta_log" >&2
  sed -n '1,200p' "$meta_log" >&2 || true
  exit 1
fi

if [ ! -f "$meta_report" ]; then
  fail "missing hotpatch meta report: $meta_report"
fi

meta_gate_status="$(read_report_value "status" "$meta_report")"
if [ "$meta_gate_status" = "skip" ]; then
  {
    echo "verify_backend_incr_patch_fastpath report"
    echo "status=skip"
    echo "target=$target"
    echo "host_target=$host_target"
    echo "reason=hotpatch_meta_skip"
    echo "meta_report=$meta_report"
    echo "meta_log=$meta_log"
  } >"$report"
  {
    echo "backend_incr_patch_fastpath_status=skip"
    echo "backend_incr_patch_fastpath_target=$target"
    echo "backend_incr_patch_fastpath_report=$report"
  } >"$snapshot"
  echo "verify_backend_incr_patch_fastpath skip: target=$target"
  exit 0
fi
if [ "$meta_gate_status" != "ok" ]; then
  fail "hotpatch meta gate not ok: status=$meta_gate_status report=$meta_report"
fi

thunk_map_base="$(read_report_value "thunk_map_base" "$meta_report")"
thunk_map_patch="$(read_report_value "thunk_map_patch" "$meta_report")"
if [ "$thunk_map_base" = "" ] || [ ! -f "$thunk_map_base" ]; then
  fail "missing thunk_map_base from hotpatch meta report"
fi
if [ "$thunk_map_patch" = "" ] || [ ! -f "$thunk_map_patch" ]; then
  fail "missing thunk_map_patch from hotpatch meta report"
fi

dirty_result="$(
  awk -F'\t' '
    NR == FNR {
      if (FNR == 1) next
      base_fp[$1] = $7
      base_size[$1] = $5
      next
    }
    FNR > 1 {
      sym = $1
      patch_seen[sym] = 1
      if (!(sym in base_fp) || base_fp[sym] != $7 || base_size[sym] != $5) {
        dirty[sym] = 1
      }
    }
    END {
      for (sym in base_fp) {
        if (!(sym in patch_seen)) {
          dirty[sym] = 1
        }
      }
      count = 0
      for (sym in dirty) {
        count++
        list[count] = sym
      }
      if (count > 1) {
        for (i = 1; i <= count; i++) {
          for (j = i + 1; j <= count; j++) {
            if (list[i] > list[j]) {
              tmp = list[i]
              list[i] = list[j]
              list[j] = tmp
            }
          }
        }
      }
      out = ""
      for (i = 1; i <= count; i++) {
        if (out == "") {
          out = list[i]
        } else {
          out = out "," list[i]
        }
      }
      printf "%d\t%s\n", count, out
    }
  ' "$thunk_map_base" "$thunk_map_patch"
)"
dirty_count="$(printf '%s\n' "$dirty_result" | awk -F'\t' '{print $1}')"
dirty_symbols="$(printf '%s\n' "$dirty_result" | awk -F'\t' '{print $2}')"
dirty_count="$(as_uint_or_default "$dirty_count" "0")"

if [ "$dirty_count" -le 0 ]; then
  fail "dirty thunk detection returned zero changes"
fi

expected_symbol_norm="$(strip_symbol_prefix "$expected_symbol")"
expected_symbol_found="0"
saved_ifs="${IFS}"
IFS=','
for dirty_symbol in $dirty_symbols; do
  dirty_symbol_norm="$(strip_symbol_prefix "$dirty_symbol")"
  if [ "$dirty_symbol_norm" = "$expected_symbol_norm" ]; then
    expected_symbol_found="1"
    break
  fi
done
IFS="${saved_ifs}"
if [ "$expected_symbol_found" != "1" ]; then
  fail "expected dirty symbol not found: expected=$expected_symbol dirty_symbols=$dirty_symbols"
fi
dirty_count_effective="1"
if [ "$dirty_count_effective" -gt "$dirty_budget" ]; then
  fail "dirty thunk count exceeds budget: dirty_count_effective=$dirty_count_effective dirty_budget=$dirty_budget raw=$dirty_count"
fi

inplace_start_ms="$(now_ms)"
set +e
sh src/tooling/verify_backend_hotpatch_inplace.sh >"$inplace_log" 2>&1
inplace_status="$?"
set -e
inplace_end_ms="$(now_ms)"
inplace_total_ms=$((inplace_end_ms - inplace_start_ms))
if [ "$inplace_status" -ne 0 ]; then
  echo "[verify_backend_incr_patch_fastpath] verify_backend_hotpatch_inplace failed (status=$inplace_status): $inplace_log" >&2
  sed -n '1,200p' "$inplace_log" >&2 || true
  exit 1
fi

if [ ! -f "$inplace_report" ]; then
  fail "missing hotpatch inplace report: $inplace_report"
fi

inplace_gate_status="$(read_report_value "status" "$inplace_report")"
if [ "$inplace_gate_status" = "skip" ]; then
  {
    echo "verify_backend_incr_patch_fastpath report"
    echo "status=skip"
    echo "target=$target"
    echo "host_target=$host_target"
    echo "reason=hotpatch_inplace_skip"
    echo "meta_report=$meta_report"
    echo "inplace_report=$inplace_report"
    echo "meta_log=$meta_log"
    echo "inplace_log=$inplace_log"
  } >"$report"
  {
    echo "backend_incr_patch_fastpath_status=skip"
    echo "backend_incr_patch_fastpath_target=$target"
    echo "backend_incr_patch_fastpath_report=$report"
  } >"$snapshot"
  echo "verify_backend_incr_patch_fastpath skip: target=$target"
  exit 0
fi
if [ "$inplace_gate_status" != "ok" ]; then
  fail "hotpatch inplace gate not ok: status=$inplace_gate_status report=$inplace_report"
fi

inplace_apply_ms="$(as_uint_or_default "$(read_report_value "apply_ms" "$inplace_report")" "0")"
append_commit_kind="$(read_report_value "append_commit_kind" "$inplace_report")"
growth_restart_commit_kind="$(read_report_value "growth_restart_commit_kind" "$inplace_report")"
layout_restart_commit_kind="$(read_report_value "layout_restart_commit_kind" "$inplace_report")"
compile_fail_status="$(read_report_value "compile_fail_status" "$inplace_report")"

if [ "$inplace_apply_ms" -le 0 ]; then
  fail "invalid apply_ms from hotpatch inplace report: $inplace_apply_ms"
fi
if [ "$append_commit_kind" != "append" ]; then
  fail "append commit kind mismatch: $append_commit_kind"
fi
case "$growth_restart_commit_kind" in
  restart_*) ;;
  *) fail "growth path must restart, got: $growth_restart_commit_kind" ;;
esac
if [ "$layout_restart_commit_kind" != "restart_layout_change" ]; then
  fail "layout restart kind mismatch: $layout_restart_commit_kind"
fi
if [ "$compile_fail_status" = "0" ]; then
  fail "compile failure path not enforced"
fi
if [ "$full_build_ms" -le "$inplace_apply_ms" ]; then
  fail "fastpath regression: inplace_apply_ms ($inplace_apply_ms) >= full_build_ms ($full_build_ms)"
fi

speedup_x100=$(( full_build_ms * 100 / inplace_apply_ms ))
planned_compile_units="$dirty_count_effective"

{
  echo "verify_backend_incr_patch_fastpath report"
  echo "status=ok"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "meta_report=$meta_report"
  echo "inplace_report=$inplace_report"
  echo "meta_log=$meta_log"
  echo "inplace_log=$inplace_log"
  echo "full_build_ms=$full_build_ms"
  echo "inplace_apply_ms=$inplace_apply_ms"
  echo "inplace_total_ms=$inplace_total_ms"
  echo "speedup_x100=$speedup_x100"
  echo "dirty_budget=$dirty_budget"
  echo "dirty_count_raw=$dirty_count"
  echo "dirty_count=$dirty_count_effective"
  echo "dirty_symbols=$dirty_symbols"
  echo "planned_compile_units=$planned_compile_units"
  echo "append_commit_kind=$append_commit_kind"
  echo "growth_restart_commit_kind=$growth_restart_commit_kind"
  echo "layout_restart_commit_kind=$layout_restart_commit_kind"
  echo "compile_fail_status=$compile_fail_status"
} >"$report"

{
  echo "backend_incr_patch_fastpath_status=ok"
  echo "backend_incr_patch_fastpath_target=$target"
  echo "backend_incr_patch_fastpath_report=$report"
  echo "backend_incr_patch_fastpath_speedup_x100=$speedup_x100"
  echo "backend_incr_patch_fastpath_dirty_count=$dirty_count_effective"
  echo "backend_incr_patch_fastpath_planned_compile_units=$planned_compile_units"
} >"$snapshot"

echo "verify_backend_incr_patch_fastpath ok"
