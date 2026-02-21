#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_selfhost_perf_regression.sh [--help]

Env:
  SELFHOST_PERF_SESSION=<name>         Session key (default: SELF_OBJ_BOOTSTRAP_SESSION or default)
  SELFHOST_PERF_TIMING=<path>          Timing tsv path (default: artifacts/backend_selfhost_self_obj/selfhost_timing_<session>.tsv)
  SELFHOST_PERF_BASELINE=<path>        Baseline env file (default: src/tooling/selfhost_perf_baseline.env)
  SELFHOST_PERF_AUTO_BUILD=<0|1>       Build timing file when missing (default: 0)
  SELFHOST_PERF_MODE=<fast|strict>     Bootstrap mode for auto-build (default: fast)
  SELFHOST_PERF_TIMEOUT=<seconds>      Bootstrap timeout for auto-build (default: 60)
  SELFHOST_PERF_REUSE=<0|1>            Bootstrap reuse for auto-build (default: 1)
  SELFHOST_PERF_STAGE0=<path>          Optional stage0 for auto-build

Limits (seconds, <=0 disables check):
  SELFHOST_PERF_MAX_LOCK_WAIT          default: 10
  SELFHOST_PERF_MAX_STAGE1             default: 30
  SELFHOST_PERF_MAX_STAGE2             default: 30
  SELFHOST_PERF_MAX_SMOKE_STAGE1       default: 20
  SELFHOST_PERF_MAX_SMOKE_STAGE2       default: 20
  SELFHOST_PERF_MAX_TOTAL              default: 60
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[selfhost_perf] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

session="${SELFHOST_PERF_SESSION:-${SELF_OBJ_BOOTSTRAP_SESSION:-default}}"
session_safe="$(printf '%s' "$session" | tr -c 'A-Za-z0-9._-' '_')"
timing_file="${SELFHOST_PERF_TIMING:-artifacts/backend_selfhost_self_obj/selfhost_timing_${session_safe}.tsv}"
auto_build="${SELFHOST_PERF_AUTO_BUILD:-0}"
auto_mode="${SELFHOST_PERF_MODE:-fast}"
auto_timeout="${SELFHOST_PERF_TIMEOUT:-60}"
auto_reuse="${SELFHOST_PERF_REUSE:-1}"
auto_stage0="${SELFHOST_PERF_STAGE0:-}"

baseline_file_default="src/tooling/selfhost_perf_baseline.env"
baseline_file="${SELFHOST_PERF_BASELINE:-$baseline_file_default}"
baseline_explicit="0"
if [ "${SELFHOST_PERF_BASELINE+x}" = "x" ]; then
  baseline_explicit="1"
fi
if [ -f "$baseline_file" ]; then
  # shellcheck disable=SC1090
  . "$baseline_file"
elif [ "$baseline_explicit" = "1" ]; then
  echo "[selfhost_perf] baseline file not found: $baseline_file" 1>&2
  exit 2
fi

max_lock_wait="${SELFHOST_PERF_MAX_LOCK_WAIT:-10}"
max_stage1="${SELFHOST_PERF_MAX_STAGE1:-30}"
max_stage2="${SELFHOST_PERF_MAX_STAGE2:-30}"
max_smoke1="${SELFHOST_PERF_MAX_SMOKE_STAGE1:-20}"
max_smoke2="${SELFHOST_PERF_MAX_SMOKE_STAGE2:-20}"
max_total="${SELFHOST_PERF_MAX_TOTAL:-60}"

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

label_duration() {
  key="$1"
  awk -F '\t' -v k="$key" '$1==k {v=$3} END{ if (v=="") exit 1; print v }' "$timing_file"
}

label_status() {
  key="$1"
  awk -F '\t' -v k="$key" '$1==k {v=$2} END{ if (v=="") exit 1; print v }' "$timing_file"
}

if [ ! -f "$timing_file" ]; then
  case "$auto_build" in
    1|true|TRUE|yes|YES|on|ON)
      echo "[selfhost_perf] timing missing; auto-build bootstrap session=$session mode=$auto_mode"
      if [ "$auto_stage0" != "" ]; then
        env SELF_OBJ_BOOTSTRAP_SESSION="$session" \
          SELF_OBJ_BOOTSTRAP_MODE="$auto_mode" \
          SELF_OBJ_BOOTSTRAP_TIMEOUT="$auto_timeout" \
          SELF_OBJ_BOOTSTRAP_REUSE="$auto_reuse" \
          SELF_OBJ_BOOTSTRAP_STAGE0="$auto_stage0" \
          sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
      else
        env SELF_OBJ_BOOTSTRAP_SESSION="$session" \
          SELF_OBJ_BOOTSTRAP_MODE="$auto_mode" \
          SELF_OBJ_BOOTSTRAP_TIMEOUT="$auto_timeout" \
          SELF_OBJ_BOOTSTRAP_REUSE="$auto_reuse" \
          sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
      fi
      ;;
    *)
      echo "[selfhost_perf] missing timing file: $timing_file" 1>&2
      echo "  hint: run src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh first or set SELFHOST_PERF_AUTO_BUILD=1" 1>&2
      exit 1
      ;;
  esac
fi

if [ ! -s "$timing_file" ]; then
  echo "[selfhost_perf] empty timing file: $timing_file" 1>&2
  exit 1
fi

failures=0

check_limit() {
  label="$1"
  max="$2"
  if ! is_enabled_limit "$max"; then
    return 0
  fi

  set +e
  duration="$(label_duration "$label" 2>/dev/null)"
  rc_dur=$?
  status="$(label_status "$label" 2>/dev/null)"
  rc_sta=$?
  set -e
  if [ "$rc_dur" -ne 0 ] || [ "$rc_sta" -ne 0 ]; then
    echo "[selfhost_perf] missing label in timing file: $label" 1>&2
    failures=$((failures + 1))
    return 0
  fi

  if ! is_uint "$duration"; then
    echo "[selfhost_perf] non-numeric duration: $label=$duration" 1>&2
    failures=$((failures + 1))
    return 0
  fi

  echo "[selfhost_perf] $label=${duration}s status=$status limit=${max}s"
  if [ "$duration" -gt "$max" ]; then
    echo "[selfhost_perf] regression: $label exceeded limit (${duration}s > ${max}s)" 1>&2
    failures=$((failures + 1))
  fi
}

check_limit "lock_wait" "$max_lock_wait"
check_limit "stage1" "$max_stage1"
check_limit "stage2" "$max_stage2"
check_limit "smoke.stage1" "$max_smoke1"
check_limit "smoke.stage2" "$max_smoke2"
check_limit "total" "$max_total"

set +e
total_status="$(label_status "total" 2>/dev/null)"
set -e
if [ "$total_status" != "" ]; then
  case "$total_status" in
    fail*|*timeout*)
      echo "[selfhost_perf] total stage is not healthy: status=$total_status" 1>&2
      failures=$((failures + 1))
      ;;
  esac
fi

if [ "$failures" -ne 0 ]; then
  echo "[selfhost_perf] FAILED (timing=$timing_file failures=$failures)" 1>&2
  exit 1
fi

echo "[selfhost_perf] ok (timing=$timing_file)"
