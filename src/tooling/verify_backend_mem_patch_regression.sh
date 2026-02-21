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
  echo "[verify_backend_mem_patch_regression] $1" >&2
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

file_digest() {
  path="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$path" | awk '{print $1}'
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$path" | awk '{print $1}'
    return 0
  fi
  cksum "$path" | awk '{print $1 ":" $2}'
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
min_speedup_x100="$(as_uint_or_default "${BACKEND_MEM_PATCH_MIN_SPEEDUP_X100:-100}" "100")"

out_dir="artifacts/backend_mem_patch_regression"
mkdir -p "$out_dir"
fastpath_log="$out_dir/backend_mem_patch_regression.$safe_target.fastpath.log"
inplace_log="$out_dir/backend_mem_patch_regression.$safe_target.inplace.log"
report="$out_dir/backend_mem_patch_regression.$safe_target.report.txt"
snapshot="$out_dir/backend_mem_patch_regression.$safe_target.snapshot.env"

rm -f "$fastpath_log" "$inplace_log" "$report" "$snapshot"

det_start_ms="$(now_ms)"
fixture="${BACKEND_DUAL_TRACK_FIXTURE:-tests/cheng/backend/fixtures/return_add.cheng}"
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
det_exe="$out_dir/determinism.out.$safe_target"
det_build_log="$out_dir/determinism.build.log"
rm -f "$det_exe" "$det_exe.o" "$det_build_log"

det_compile() {
  preferred_linker="${BACKEND_HOTPATCH_GATE_LINKER:-self}"
  no_runtime_c="1"
  if [ "$preferred_linker" = "system" ]; then
    no_runtime_c="0"
  fi
  set +e
  env \
    ABI=v2_noptr \
    BACKEND_TARGET="$target" \
    BACKEND_LINKER="$preferred_linker" \
    BACKEND_RUNTIME=off \
    BACKEND_NO_RUNTIME_C="$no_runtime_c" \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
    STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
    sh src/tooling/chengc.sh "$fixture" --skip-pkg --out:"$det_exe" >"$det_build_log" 2>&1
  status="$?"
  if [ "$status" -ne 0 ] && [ "$preferred_linker" = "self" ]; then
    env \
      ABI=v2_noptr \
      BACKEND_TARGET="$target" \
      BACKEND_LINKER=system \
      BACKEND_RUNTIME=off \
      BACKEND_NO_RUNTIME_C=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
      STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
      sh src/tooling/chengc.sh "$fixture" --skip-pkg --out:"$det_exe" >>"$det_build_log" 2>&1
    status="$?"
  fi
  set -e
  return "$status"
}

if ! det_compile; then
  echo "[verify_backend_mem_patch_regression] determinism compile failed: $det_build_log" >&2
  sed -n '1,160p' "$det_build_log" >&2 || true
  exit 1
fi
sha_a="$(file_digest "$det_exe")"
if ! det_compile; then
  echo "[verify_backend_mem_patch_regression] determinism compile second run failed: $det_build_log" >&2
  sed -n '1,160p' "$det_build_log" >&2 || true
  exit 1
fi
sha_b="$(file_digest "$det_exe")"
det_end_ms="$(now_ms)"
det_ms=$((det_end_ms - det_start_ms))
if [ "$sha_a" = "" ] || [ "$sha_b" = "" ] || [ "$sha_a" != "$sha_b" ]; then
  fail "determinism mismatch: sha_a=$sha_a sha_b=$sha_b (log=$det_build_log)"
fi
det_digest="$sha_a"

fastpath_start_ms="$(now_ms)"
set +e
sh src/tooling/verify_backend_incr_patch_fastpath.sh >"$fastpath_log" 2>&1
fastpath_status_code="$?"
set -e
fastpath_end_ms="$(now_ms)"
fastpath_gate_ms=$((fastpath_end_ms - fastpath_start_ms))
if [ "$fastpath_status_code" -ne 0 ]; then
  echo "[verify_backend_mem_patch_regression] incr_patch_fastpath gate failed (status=$fastpath_status_code): $fastpath_log" >&2
  sed -n '1,200p' "$fastpath_log" >&2 || true
  exit 1
fi

fastpath_report="artifacts/backend_incr_patch_fastpath/backend_incr_patch_fastpath.$safe_target.report.txt"
if [ ! -f "$fastpath_report" ]; then
  fail "missing incr_patch_fastpath report: $fastpath_report"
fi
fastpath_status="$(read_report_value "status" "$fastpath_report")"
if [ "$fastpath_status" = "skip" ]; then
  {
    echo "verify_backend_mem_patch_regression report"
    echo "status=skip"
    echo "target=$target"
    echo "host_target=$host_target"
    echo "reason=incr_patch_fastpath_skip"
    echo "determinism_log=$det_build_log"
    echo "fastpath_log=$fastpath_log"
    echo "fastpath_report=$fastpath_report"
  } >"$report"
  {
    echo "backend_mem_patch_regression_status=skip"
    echo "backend_mem_patch_regression_target=$target"
    echo "backend_mem_patch_regression_report=$report"
  } >"$snapshot"
  echo "verify_backend_mem_patch_regression skip: target=$target"
  exit 0
fi
if [ "$fastpath_status" != "ok" ]; then
  fail "incr_patch_fastpath status is not ok: $fastpath_status"
fi

full_build_ms="$(as_uint_or_default "$(read_report_value "full_build_ms" "$fastpath_report")" "0")"
fastpath_apply_ms="$(as_uint_or_default "$(read_report_value "inplace_apply_ms" "$fastpath_report")" "0")"
speedup_x100="$(as_uint_or_default "$(read_report_value "speedup_x100" "$fastpath_report")" "0")"
dirty_count="$(as_uint_or_default "$(read_report_value "dirty_count" "$fastpath_report")" "0")"
if [ "$full_build_ms" -le 0 ] || [ "$fastpath_apply_ms" -le 0 ]; then
  fail "invalid latency fields in fastpath report: full_build_ms=$full_build_ms inplace_apply_ms=$fastpath_apply_ms"
fi
if [ "$speedup_x100" -lt "$min_speedup_x100" ]; then
  fail "latency regression: speedup_x100=$speedup_x100 min_speedup_x100=$min_speedup_x100"
fi

inplace_start_ms="$(now_ms)"
set +e
sh src/tooling/verify_backend_hotpatch_inplace.sh >"$inplace_log" 2>&1
inplace_status_code="$?"
set -e
inplace_end_ms="$(now_ms)"
inplace_gate_ms=$((inplace_end_ms - inplace_start_ms))
if [ "$inplace_status_code" -ne 0 ]; then
  echo "[verify_backend_mem_patch_regression] hotpatch_inplace gate failed (status=$inplace_status_code): $inplace_log" >&2
  sed -n '1,200p' "$inplace_log" >&2 || true
  exit 1
fi

inplace_report="artifacts/backend_hotpatch_inplace/backend_hotpatch_inplace.$safe_target.report.txt"
if [ ! -f "$inplace_report" ]; then
  fail "missing hotpatch_inplace report: $inplace_report"
fi
inplace_status="$(read_report_value "status" "$inplace_report")"
if [ "$inplace_status" = "skip" ]; then
  {
    echo "verify_backend_mem_patch_regression report"
    echo "status=skip"
    echo "target=$target"
    echo "host_target=$host_target"
    echo "reason=hotpatch_inplace_skip"
    echo "determinism_log=$det_build_log"
    echo "fastpath_log=$fastpath_log"
    echo "inplace_log=$inplace_log"
    echo "fastpath_report=$fastpath_report"
    echo "inplace_report=$inplace_report"
  } >"$report"
  {
    echo "backend_mem_patch_regression_status=skip"
    echo "backend_mem_patch_regression_target=$target"
    echo "backend_mem_patch_regression_report=$report"
  } >"$snapshot"
  echo "verify_backend_mem_patch_regression skip: target=$target"
  exit 0
fi
if [ "$inplace_status" != "ok" ]; then
  fail "hotpatch_inplace status is not ok: $inplace_status"
fi

inplace_apply_ms="$(as_uint_or_default "$(read_report_value "apply_ms" "$inplace_report")" "0")"
append_commit_kind="$(read_report_value "append_commit_kind" "$inplace_report")"
growth_restart_commit_kind="$(read_report_value "growth_restart_commit_kind" "$inplace_report")"
layout_restart_commit_kind="$(read_report_value "layout_restart_commit_kind" "$inplace_report")"
compile_fail_status="$(as_uint_or_default "$(read_report_value "compile_fail_status" "$inplace_report")" "0")"
host_pid="$(read_report_value "host_pid" "$inplace_report")"
host_pid_final="$(read_report_value "host_pid_final" "$inplace_report")"
marker_tx_total="$(as_uint_or_default "$(read_report_value "marker_tx_total" "$inplace_report")" "0")"

if [ "$append_commit_kind" != "append" ]; then
  fail "append commit mismatch: $append_commit_kind"
fi
case "$growth_restart_commit_kind" in
  restart_*) ;;
  *) fail "growth restart path missing: $growth_restart_commit_kind" ;;
esac
if [ "$layout_restart_commit_kind" != "restart_layout_change" ]; then
  fail "layout restart path missing: $layout_restart_commit_kind"
fi
if [ "$compile_fail_status" = "0" ]; then
  fail "compile failure rollback path missing"
fi
if [ "$host_pid" = "" ] || [ "$host_pid_final" = "" ] || [ "$host_pid" != "$host_pid_final" ]; then
  fail "host pid stability regression: host_pid=$host_pid host_pid_final=$host_pid_final"
fi
if [ "$marker_tx_total" -lt 4 ]; then
  fail "transaction markers insufficient: marker_tx_total=$marker_tx_total"
fi

{
  echo "verify_backend_mem_patch_regression report"
  echo "status=ok"
  echo "target=$target"
  echo "host_target=$host_target"
  echo "determinism_ok=1"
  echo "latency_ok=1"
  echo "append_commit_ok=1"
  echo "restart_fallback_ok=1"
  echo "pid_stable_ok=1"
  echo "determinism_gate=chengc_double_build"
  echo "determinism_gate_ms=$det_ms"
  echo "determinism_exe=$det_exe"
  echo "determinism_digest=$det_digest"
  echo "determinism_log=$det_build_log"
  echo "fastpath_gate=src/tooling/verify_backend_incr_patch_fastpath.sh"
  echo "fastpath_gate_ms=$fastpath_gate_ms"
  echo "fastpath_report=$fastpath_report"
  echo "fastpath_log=$fastpath_log"
  echo "full_build_ms=$full_build_ms"
  echo "fastpath_apply_ms=$fastpath_apply_ms"
  echo "speedup_x100=$speedup_x100"
  echo "min_speedup_x100=$min_speedup_x100"
  echo "dirty_count=$dirty_count"
  echo "inplace_gate=src/tooling/verify_backend_hotpatch_inplace.sh"
  echo "inplace_gate_ms=$inplace_gate_ms"
  echo "inplace_report=$inplace_report"
  echo "inplace_log=$inplace_log"
  echo "inplace_apply_ms=$inplace_apply_ms"
  echo "append_commit_kind=$append_commit_kind"
  echo "growth_restart_commit_kind=$growth_restart_commit_kind"
  echo "layout_restart_commit_kind=$layout_restart_commit_kind"
  echo "compile_fail_status=$compile_fail_status"
  echo "host_pid=$host_pid"
  echo "host_pid_final=$host_pid_final"
  echo "marker_tx_total=$marker_tx_total"
} >"$report"

{
  echo "backend_mem_patch_regression_status=ok"
  echo "backend_mem_patch_regression_target=$target"
  echo "backend_mem_patch_regression_report=$report"
  echo "backend_mem_patch_regression_speedup_x100=$speedup_x100"
  echo "backend_mem_patch_regression_dirty_count=$dirty_count"
  echo "backend_mem_patch_regression_host_pid=$host_pid"
  echo "backend_mem_patch_regression_host_pid_final=$host_pid_final"
} >"$snapshot"

echo "verify_backend_mem_patch_regression ok"
