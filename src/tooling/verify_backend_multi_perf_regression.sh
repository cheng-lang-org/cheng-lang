#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_multi_perf_regression.sh [--help]

Env:
  MULTI_PERF_SESSION=<name>        Session key (default: default)
  MULTI_PERF_TIMING=<path>         Timing tsv path (default: artifacts/backend_multi/multi_perf_timing_<session>.tsv)
  MULTI_PERF_BASELINE=<path>       Baseline env file (default: src/tooling/multi_perf_baseline.env)
  MULTI_PERF_TIMEOUT=<seconds>     Per-gate timeout (default: 60; <=0 disables timeout)

Limits (seconds, <=0 disables check):
  MULTI_PERF_MAX_MULTI             default: 30
  MULTI_PERF_MAX_MULTI_LTO         default: 30
  MULTI_PERF_MAX_TOTAL             default: 60
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[multi_perf] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

session="${MULTI_PERF_SESSION:-default}"
session_safe="$(printf '%s' "$session" | tr -c 'A-Za-z0-9._-' '_')"
timing_file="${MULTI_PERF_TIMING:-artifacts/backend_multi/multi_perf_timing_${session_safe}.tsv}"
timing_dir="$(dirname -- "$timing_file")"
mkdir -p "$timing_dir"
: > "$timing_file"

baseline_file_default="src/tooling/multi_perf_baseline.env"
baseline_file="${MULTI_PERF_BASELINE:-$baseline_file_default}"
baseline_explicit="0"
if [ "${MULTI_PERF_BASELINE+x}" = "x" ]; then
  baseline_explicit="1"
fi
if [ -f "$baseline_file" ]; then
  # shellcheck disable=SC1090
  . "$baseline_file"
elif [ "$baseline_explicit" = "1" ]; then
  echo "[multi_perf] baseline file not found: $baseline_file" 1>&2
  exit 2
fi

max_multi="${MULTI_PERF_MAX_MULTI:-30}"
max_multi_lto="${MULTI_PERF_MAX_MULTI_LTO:-30}"
max_total="${MULTI_PERF_MAX_TOTAL:-60}"
gate_timeout="${MULTI_PERF_TIMEOUT:-60}"

is_uint() {
  case "$1" in
    ''|*[!0-9]*)
      return 1
      ;;
  esac
  return 0
}

is_enabled_limit() {
  limit="$1"
  if ! is_uint "$limit"; then
    return 1
  fi
  [ "$limit" -gt 0 ]
}

timeout_cmd=""
if command -v timeout >/dev/null 2>&1; then
  timeout_cmd="timeout"
elif command -v gtimeout >/dev/null 2>&1; then
  timeout_cmd="gtimeout"
fi

if [ "$timeout_cmd" = "" ] && is_enabled_limit "$gate_timeout"; then
  echo "[multi_perf] warn: timeout command not found; run without per-gate timeout" 1>&2
fi

timestamp_now() {
  date +%s
}

failures=0

check_duration_limit() {
  label="$1"
  duration="$2"
  limit="$3"
  if ! is_enabled_limit "$limit"; then
    return 0
  fi
  if [ "$duration" -gt "$limit" ]; then
    echo "[multi_perf] regression: $label exceeded limit (${duration}s > ${limit}s)" 1>&2
    failures=$((failures + 1))
  fi
}

run_gate() {
  label="$1"
  limit="$2"
  script_path="$3"
  start="$(timestamp_now)"
  set +e
  if [ "$timeout_cmd" != "" ] && is_enabled_limit "$gate_timeout"; then
    "$timeout_cmd" "${gate_timeout}s" sh "$script_path"
    rc="$?"
  else
    sh "$script_path"
    rc="$?"
  fi
  set -e
  end="$(timestamp_now)"
  duration=$((end - start))
  status="ok"
  if [ "$rc" -eq 124 ]; then
    status="timeout"
  elif [ "$rc" -ne 0 ]; then
    status="fail"
  fi
  printf '%s\t%s\t%s\n' "$label" "$status" "$duration" >>"$timing_file"
  echo "[multi_perf] $label=${duration}s status=$status limit=${limit}s"
  if [ "$rc" -ne 0 ]; then
    failures=$((failures + 1))
  fi
  check_duration_limit "$label" "$duration" "$limit"
}

total_start="$(timestamp_now)"
run_gate "multi" "$max_multi" "src/tooling/verify_backend_multi.sh"
run_gate "multi_lto" "$max_multi_lto" "src/tooling/verify_backend_multi_lto.sh"
total_end="$(timestamp_now)"
total_duration=$((total_end - total_start))
total_status="ok"
if [ "$failures" -ne 0 ]; then
  total_status="fail"
fi
printf '%s\t%s\t%s\n' "total" "$total_status" "$total_duration" >>"$timing_file"
echo "[multi_perf] total=${total_duration}s status=$total_status limit=${max_total}s"
check_duration_limit "total" "$total_duration" "$max_total"

if [ "$failures" -ne 0 ]; then
  echo "[multi_perf] FAILED (timing=$timing_file failures=$failures)" 1>&2
  exit 1
fi

echo "[multi_perf] ok (timing=$timing_file)"
