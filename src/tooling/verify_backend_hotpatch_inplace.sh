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
  echo "[verify_backend_hotpatch_inplace] $1" >&2
  exit 1
}

count_marker() {
  file="$1"
  pattern="$2"
  matches="$(rg -n --no-messages -e "$pattern" "$file" || true)"
  if [ "$matches" = "" ]; then
    echo "0"
    return
  fi
  printf '%s\n' "$matches" | wc -l | tr -d ' '
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

out_dir="artifacts/backend_hotpatch_inplace"
mkdir -p "$out_dir"
inner_log="$out_dir/hotpatch_inplace.$safe_target.inner.log"
report="$out_dir/backend_hotpatch_inplace.$safe_target.report.txt"
snapshot="$out_dir/backend_hotpatch_inplace.$safe_target.snapshot.env"

rm -f "$inner_log" "$report" "$snapshot"

runner_file="src/tooling/backend_host_runner.sh"
apply_file="src/tooling/backend_hotpatch_apply.sh"
if [ ! -f "$runner_file" ]; then
  fail "missing host runner script: $runner_file"
fi
if [ ! -f "$apply_file" ]; then
  fail "missing apply script: $apply_file"
fi

marker_append="$(count_marker "$runner_file" 'commit_kind="append"')"
marker_restart_layout="$(count_marker "$runner_file" 'restart_reason="layout_change"')"
marker_restart_pool="$(count_marker "$runner_file" 'restart_reason="pool_exhausted"')"
marker_backup_meta="$(count_marker "$runner_file" 'meta.env.bak')"
marker_backup_pool="$(count_marker "$runner_file" 'code_pool.bin.bak')"
marker_backup_exec="$(count_marker "$runner_file" 'current.exe.bak')"
marker_apply_delegate="$(count_marker "$apply_file" 'commit_args="--state:')"
marker_append_total=$((marker_append + marker_restart_layout + marker_restart_pool))
marker_tx_total=$((marker_backup_meta + marker_backup_pool + marker_backup_exec + marker_apply_delegate))

if [ "$marker_append_total" -lt 3 ]; then
  fail "missing append/restart transaction markers in $runner_file"
fi
if [ "$marker_tx_total" -lt 4 ]; then
  fail "missing rollback safety markers in runner/apply scripts"
fi

set +e
sh src/tooling/verify_backend_hotpatch.sh >"$inner_log" 2>&1
inner_status="$?"
set -e
if [ "$inner_status" -ne 0 ]; then
  echo "[verify_backend_hotpatch_inplace] underlying hotpatch gate failed (status=$inner_status): $inner_log" >&2
  sed -n '1,200p' "$inner_log" >&2 || true
  exit 1
fi

inner_report="artifacts/backend_hotpatch/hotpatch.$safe_target.report.txt"
if [ ! -f "$inner_report" ]; then
  fail "missing underlying report: $inner_report"
fi

status="$(read_report_value "status" "$inner_report")"
append_commit_kind="$(read_report_value "append_commit_kind" "$inner_report")"
growth_commit_kind="$(read_report_value "growth_restart_commit_kind" "$inner_report")"
layout_commit_kind="$(read_report_value "layout_restart_commit_kind" "$inner_report")"
compile_fail_status="$(read_report_value "compile_fail_status" "$inner_report")"
apply_ms="$(read_report_value "apply_ms" "$inner_report")"
host_pid="$(read_report_value "host_pid" "$inner_report")"
host_pid_final="$(read_report_value "host_pid_final" "$inner_report")"

if [ "$status" = "" ]; then
  fail "underlying report missing status field: $inner_report"
fi
if [ "$status" = "ok" ]; then
  if [ "$append_commit_kind" != "append" ]; then
    fail "append commit kind mismatch: $append_commit_kind"
  fi
  case "$growth_commit_kind" in
    restart_*) ;;
    *) fail "growth path must restart, got: $growth_commit_kind" ;;
  esac
  if [ "$layout_commit_kind" != "restart_layout_change" ]; then
    fail "layout-change path must restart_layout_change, got: $layout_commit_kind"
  fi
  if [ "$compile_fail_status" = "0" ]; then
    fail "compile failure path did not fail"
  fi
  if [ "$host_pid" = "" ] || [ "$host_pid_final" = "" ] || [ "$host_pid" != "$host_pid_final" ]; then
    fail "host pid drift detected: host_pid=$host_pid host_pid_final=$host_pid_final"
  fi
fi

{
  echo "verify_backend_hotpatch_inplace report"
  echo "status=$status"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "inner_gate=src/tooling/verify_backend_hotpatch.sh"
  echo "inner_status=$inner_status"
  echo "inner_report=$inner_report"
  echo "inner_log=$inner_log"
  echo "apply_ms=$apply_ms"
  echo "append_commit_kind=$append_commit_kind"
  echo "growth_restart_commit_kind=$growth_commit_kind"
  echo "layout_restart_commit_kind=$layout_commit_kind"
  echo "compile_fail_status=$compile_fail_status"
  echo "host_pid=$host_pid"
  echo "host_pid_final=$host_pid_final"
  echo "marker_append=$marker_append"
  echo "marker_restart_layout=$marker_restart_layout"
  echo "marker_restart_pool=$marker_restart_pool"
  echo "marker_backup_meta=$marker_backup_meta"
  echo "marker_backup_pool=$marker_backup_pool"
  echo "marker_backup_exec=$marker_backup_exec"
  echo "marker_apply_delegate=$marker_apply_delegate"
  echo "marker_append_total=$marker_append_total"
  echo "marker_tx_total=$marker_tx_total"
} >"$report"

{
  echo "backend_hotpatch_inplace_status=$status"
  echo "backend_hotpatch_inplace_target=$target"
  echo "backend_hotpatch_inplace_report=$report"
  echo "backend_hotpatch_inplace_marker_append_total=$marker_append_total"
  echo "backend_hotpatch_inplace_marker_tx_total=$marker_tx_total"
  echo "backend_hotpatch_inplace_host_pid=$host_pid"
  echo "backend_hotpatch_inplace_host_pid_final=$host_pid_final"
} >"$snapshot"

if [ "$status" = "skip" ]; then
  echo "verify_backend_hotpatch_inplace skip: target=$target"
  exit 0
fi

if [ "$status" != "ok" ]; then
  fail "underlying hotpatch gate status=$status (see $inner_report)"
fi

echo "verify_backend_hotpatch_inplace ok"
