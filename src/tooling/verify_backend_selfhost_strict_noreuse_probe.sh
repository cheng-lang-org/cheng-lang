#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh [--help]

Env:
  CHENG_SELFHOST_STRICT_PROBE_SESSION=<name>             default: prod.strict.noreuse
  CHENG_SELFHOST_STRICT_PROBE_TIMEOUT=<seconds>          default: 60
  CHENG_SELFHOST_STRICT_PROBE_REQUIRE=<0|1>              default: 0 (soft probe)
  CHENG_SELFHOST_STRICT_PROBE_STAGE0=<path>              optional stage0 override
  CHENG_SELFHOST_STRICT_PROBE_REUSE=<0|1>                default: 0
  CHENG_SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE=<0|1> default: 0
  CHENG_SELFHOST_STRICT_PROBE_ALLOW_STAGE0_FALLBACK=<0|1>   default: 0
  CHENG_SELFHOST_STRICT_PROBE_SKIP_SEM=<0|1>             default: 1
  CHENG_SELFHOST_STRICT_PROBE_SKIP_OWNERSHIP=<0|1>       default: 1
  CHENG_SELFHOST_STRICT_PROBE_GENERIC_MODE=<mode>        default: hybrid
  CHENG_SELFHOST_STRICT_PROBE_GENERIC_SPEC_BUDGET=<N>    default: 0
  CHENG_SELFHOST_STRICT_PROBE_TIMING=<path>              optional timing file override

Notes:
  - Runs strict selfhost bootstrap with reuse disabled, intended as a cold-path
    performance probe.
  - In soft mode (default), failure is reported but does not fail the script.
  - Set CHENG_SELFHOST_STRICT_PROBE_REQUIRE=1 to make it blocking.
EOF
}

is_true() {
  case "${1:-}" in
    1|true|TRUE|yes|YES|on|ON)
      return 0
      ;;
  esac
  return 1
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[selfhost_strict_probe] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

session="${CHENG_SELFHOST_STRICT_PROBE_SESSION:-prod.strict.noreuse}"
session_safe="$(printf '%s' "$session" | tr -c 'A-Za-z0-9._-' '_')"
timeout="${CHENG_SELFHOST_STRICT_PROBE_TIMEOUT:-60}"
require_probe="${CHENG_SELFHOST_STRICT_PROBE_REQUIRE:-0}"
stage0="${CHENG_SELFHOST_STRICT_PROBE_STAGE0:-}"
reuse="${CHENG_SELFHOST_STRICT_PROBE_REUSE:-0}"
strict_allow_fast_reuse="${CHENG_SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE:-0}"
allow_stage0_fallback="${CHENG_SELFHOST_STRICT_PROBE_ALLOW_STAGE0_FALLBACK:-0}"
skip_sem="${CHENG_SELFHOST_STRICT_PROBE_SKIP_SEM:-1}"
skip_own="${CHENG_SELFHOST_STRICT_PROBE_SKIP_OWNERSHIP:-1}"
generic_mode="${CHENG_SELFHOST_STRICT_PROBE_GENERIC_MODE:-hybrid}"
generic_budget="${CHENG_SELFHOST_STRICT_PROBE_GENERIC_SPEC_BUDGET:-0}"
timing_file="${CHENG_SELFHOST_STRICT_PROBE_TIMING:-artifacts/backend_selfhost_self_obj/selfhost_timing_${session_safe}.tsv}"

echo "== backend.selfhost_strict_noreuse_probe =="
echo "[selfhost_strict_probe] session=$session timeout=${timeout}s mode=strict reuse=$reuse allow_stage0_fallback=$allow_stage0_fallback"

if [ "$stage0" != "" ]; then
  set +e
  env \
    CHENG_SELF_OBJ_BOOTSTRAP_MODE=strict \
    CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$timeout" \
    CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$session" \
    CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$reuse" \
    CHENG_SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" \
    CHENG_BACKEND_BUILD_DRIVER_ALLOW_STAGE0_FALLBACK="$allow_stage0_fallback" \
    CHENG_STAGE1_SKIP_SEM="$skip_sem" \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_own" \
    CHENG_GENERIC_MODE="$generic_mode" \
    CHENG_GENERIC_SPEC_BUDGET="$generic_budget" \
    CHENG_SELF_OBJ_BOOTSTRAP_STAGE0="$stage0" \
    sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  rc="$?"
  set -e
else
  set +e
  env \
    CHENG_SELF_OBJ_BOOTSTRAP_MODE=strict \
    CHENG_SELF_OBJ_BOOTSTRAP_TIMEOUT="$timeout" \
    CHENG_SELF_OBJ_BOOTSTRAP_SESSION="$session" \
    CHENG_SELF_OBJ_BOOTSTRAP_REUSE="$reuse" \
    CHENG_SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" \
    CHENG_BACKEND_BUILD_DRIVER_ALLOW_STAGE0_FALLBACK="$allow_stage0_fallback" \
    CHENG_STAGE1_SKIP_SEM="$skip_sem" \
    CHENG_STAGE1_SKIP_OWNERSHIP="$skip_own" \
    CHENG_GENERIC_MODE="$generic_mode" \
    CHENG_GENERIC_SPEC_BUDGET="$generic_budget" \
    sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  rc="$?"
  set -e
fi

if [ "$rc" -eq 0 ]; then
  echo "[selfhost_strict_probe] ok"
  exit 0
fi

echo "[selfhost_strict_probe] warn: strict no-reuse probe failed (status=$rc)" 1>&2
if [ -s "$timing_file" ]; then
  total_status="$(awk -F '\t' '$1=="total" {v=$2} END{print v}' "$timing_file" 2>/dev/null || true)"
  total_sec="$(awk -F '\t' '$1=="total" {v=$3} END{print v}' "$timing_file" 2>/dev/null || true)"
  if [ "$total_status" != "" ] || [ "$total_sec" != "" ]; then
    echo "[selfhost_strict_probe] timing: total=${total_sec:-?}s status=${total_status:-?} file=$timing_file" 1>&2
  else
    echo "[selfhost_strict_probe] timing file present but total row missing: $timing_file" 1>&2
  fi
else
  echo "[selfhost_strict_probe] timing file missing: $timing_file" 1>&2
fi

if is_true "$require_probe"; then
  echo "[selfhost_strict_probe] required probe failed" 1>&2
  exit 1
fi

echo "[selfhost_strict_probe] soft probe only; continue" 1>&2
exit 0
