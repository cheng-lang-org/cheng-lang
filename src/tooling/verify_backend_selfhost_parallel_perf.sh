#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_selfhost_parallel_perf.sh [--help]

Env:
  SELFHOST_PARALLEL_PERF_TIMEOUT=<seconds>          default: 80
  SELFHOST_PARALLEL_PERF_STAGE0=<path>              optional stage0 override
  SELFHOST_PARALLEL_PERF_BASE_SESSION=<name>        default: perf.parallel
  SELFHOST_PARALLEL_PERF_JOBS=<N>                   default: 0 (auto)
  SELFHOST_PARALLEL_PERF_MAX_SLOWDOWN_SEC=<N>       default: 2
  SELFHOST_PARALLEL_PERF_SKIP_SMOKE=<0|1>           default: 1
  SELFHOST_PARALLEL_PERF_REQUIRE_RUNNABLE=<0|1>     default: 1
  SELFHOST_PARALLEL_PERF_VALIDATE=<0|1>             default: 0
  SELFHOST_PARALLEL_PERF_SKIP_SEM=<0|1>             default: 1
  SELFHOST_PARALLEL_PERF_SKIP_OWNERSHIP=<0|1>       default: 1
  SELFHOST_PARALLEL_PERF_SKIP_CPROFILE=<0|1>        default: 1
  SELFHOST_PARALLEL_PERF_ALLOW_RETRY=<0|1>          default: 1
  SELFHOST_PARALLEL_PERF_GENERIC_MODE=<mode>        default: dict
  SELFHOST_PARALLEL_PERF_GENERIC_SPEC_BUDGET=<N>    default: 0
  SELFHOST_PARALLEL_PERF_REPORT=<path>              optional report path

Notes:
  - Runs strict no-reuse probe twice with same stage0/settings:
      1) serial  (multi=0)
      2) parallel (multi=1, multi_force=0)
  - Passes only when both runs pass and parallel total <= serial total + max_slowdown.
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[selfhost_parallel_perf] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

timeout="${SELFHOST_PARALLEL_PERF_TIMEOUT:-80}"
stage0="${SELFHOST_PARALLEL_PERF_STAGE0:-}"
base_session="${SELFHOST_PARALLEL_PERF_BASE_SESSION:-perf.parallel}"
jobs="${SELFHOST_PARALLEL_PERF_JOBS:-0}"
max_slowdown="${SELFHOST_PARALLEL_PERF_MAX_SLOWDOWN_SEC:-2}"
skip_smoke="${SELFHOST_PARALLEL_PERF_SKIP_SMOKE:-1}"
require_runnable="${SELFHOST_PARALLEL_PERF_REQUIRE_RUNNABLE:-1}"
validate="${SELFHOST_PARALLEL_PERF_VALIDATE:-0}"
skip_sem="${SELFHOST_PARALLEL_PERF_SKIP_SEM:-1}"
skip_own="${SELFHOST_PARALLEL_PERF_SKIP_OWNERSHIP:-1}"
skip_cprofile="${SELFHOST_PARALLEL_PERF_SKIP_CPROFILE:-1}"
allow_retry="${SELFHOST_PARALLEL_PERF_ALLOW_RETRY:-1}"
generic_mode="${SELFHOST_PARALLEL_PERF_GENERIC_MODE:-dict}"
generic_budget="${SELFHOST_PARALLEL_PERF_GENERIC_SPEC_BUDGET:-0}"

report="${SELFHOST_PARALLEL_PERF_REPORT:-artifacts/backend_selfhost_self_obj/selfhost_parallel_perf_$(printf '%s' "$base_session" | tr -c 'A-Za-z0-9._-' '_').tsv}"
report_dir="$(dirname "$report")"
mkdir -p "$report_dir" 2>/dev/null || true

if [ "$stage0" = "" ]; then
  stage0="$(sh src/tooling/backend_driver_path.sh)"
fi

if [ ! -x "$stage0" ]; then
  echo "[selfhost_parallel_perf] missing stage0: $stage0" 1>&2
  exit 1
fi

run_probe() {
  label="$1"
  multi="$2"
  multi_force="$3"
  session="${base_session}.${label}"
  session_safe="$(printf '%s' "$session" | tr -c 'A-Za-z0-9._-' '_')"
  timing="artifacts/backend_selfhost_self_obj/selfhost_timing_${session_safe}.tsv"
  rm -f "$timing"

  env \
    SELFHOST_STRICT_PROBE_SESSION="$session" \
    SELFHOST_STRICT_PROBE_TIMEOUT="$timeout" \
    SELFHOST_STRICT_PROBE_REQUIRE=1 \
    SELFHOST_STRICT_PROBE_STAGE0="$stage0" \
    SELFHOST_STRICT_PROBE_REUSE=0 \
    SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE=0 \
    SELFHOST_STRICT_PROBE_SKIP_SEM="$skip_sem" \
    SELFHOST_STRICT_PROBE_SKIP_OWNERSHIP="$skip_own" \
    SELFHOST_STRICT_PROBE_SKIP_CPROFILE="$skip_cprofile" \
    SELFHOST_STRICT_PROBE_GENERIC_MODE="$generic_mode" \
    SELFHOST_STRICT_PROBE_GENERIC_SPEC_BUDGET="$generic_budget" \
    SELFHOST_STRICT_PROBE_VALIDATE="$validate" \
    SELFHOST_STRICT_PROBE_SKIP_SMOKE="$skip_smoke" \
    SELFHOST_STRICT_PROBE_REQUIRE_RUNNABLE="$require_runnable" \
    SELFHOST_STRICT_PROBE_ALLOW_RETRY="$allow_retry" \
    SELFHOST_STRICT_PROBE_MULTI="$multi" \
    SELFHOST_STRICT_PROBE_MULTI_FORCE="$multi_force" \
    SELFHOST_STRICT_PROBE_JOBS="$jobs" \
    SELFHOST_STRICT_PROBE_TIMING="$timing" \
    sh src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh

  total_status="$(awk -F '\t' '$1=="total" {v=$2} END{print v}' "$timing" 2>/dev/null || true)"
  total_sec="$(awk -F '\t' '$1=="total" {v=$3} END{print v}' "$timing" 2>/dev/null || true)"
  stage1_sec="$(awk -F '\t' '$1=="stage1" {v=$3} END{print v}' "$timing" 2>/dev/null || true)"
  if [ "$total_sec" = "" ]; then
    total_sec="-1"
  fi
  if [ "$stage1_sec" = "" ]; then
    stage1_sec="-1"
  fi
  if [ "$total_status" = "" ]; then
    total_status="unknown"
  fi
  printf '%s\t%s\t%s\t%s\t%s\n' "$label" "$total_status" "$total_sec" "$stage1_sec" "$timing" >>"$report"
}

: >"$report"
printf 'mode\tstatus\ttotal_sec\tstage1_sec\ttiming_file\n' >>"$report"

echo "== backend.selfhost_parallel_perf.serial =="
run_probe "serial" "0" "0"

echo "== backend.selfhost_parallel_perf.parallel =="
run_probe "parallel" "1" "0"

serial_status="$(awk -F '\t' '$1=="serial" {print $2}' "$report" | tail -n 1)"
serial_total="$(awk -F '\t' '$1=="serial" {print $3}' "$report" | tail -n 1)"
parallel_status="$(awk -F '\t' '$1=="parallel" {print $2}' "$report" | tail -n 1)"
parallel_total="$(awk -F '\t' '$1=="parallel" {print $3}' "$report" | tail -n 1)"

echo "[selfhost_parallel_perf] serial=${serial_total}s status=${serial_status}"
echo "[selfhost_parallel_perf] parallel=${parallel_total}s status=${parallel_status}"

if [ "$serial_status" != "ok" ] || [ "$parallel_status" != "ok" ]; then
  echo "[selfhost_parallel_perf] fail: probe status is not ok (report=$report)" 1>&2
  exit 1
fi

case "$serial_total:$parallel_total:$max_slowdown" in
  *[!0-9:-]*)
    echo "[selfhost_parallel_perf] fail: non-numeric totals (report=$report)" 1>&2
    exit 1
    ;;
esac

limit=$((serial_total + max_slowdown))
if [ "$parallel_total" -gt "$limit" ]; then
  echo "[selfhost_parallel_perf] fail: parallel slower than budget (parallel=${parallel_total}s > serial=${serial_total}s + ${max_slowdown}s, report=$report)" 1>&2
  exit 1
fi

echo "[selfhost_parallel_perf] ok (report=$report)"
