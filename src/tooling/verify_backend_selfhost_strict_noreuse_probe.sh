#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_selfhost_strict_noreuse_probe.sh [--help]

Env:
  SELFHOST_STRICT_PROBE_SESSION=<name>             default: prod.strict.noreuse
  SELFHOST_STRICT_PROBE_TIMEOUT=<seconds>          default: 60
  SELFHOST_STRICT_PROBE_REQUIRE=<0|1>              default: 0 (soft probe)
  SELFHOST_STRICT_PROBE_STAGE0=<path>              optional stage0 override
  SELFHOST_STRICT_PROBE_REUSE=<0|1>                default: 0
  SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE=<0|1> default: 0
  SELFHOST_STRICT_PROBE_SKIP_SEM=<0|1>             default: 1
  SELFHOST_STRICT_PROBE_SKIP_OWNERSHIP=<0|1>       default: 1
  SELFHOST_STRICT_PROBE_SKIP_CPROFILE=<0|1>        default: 1
  SELFHOST_STRICT_PROBE_GENERIC_MODE=<mode>        default: dict
  SELFHOST_STRICT_PROBE_GENERIC_SPEC_BUDGET=<N>    default: 0
  SELFHOST_STRICT_PROBE_VALIDATE=<0|1>             default: 0
  SELFHOST_STRICT_PROBE_SKIP_SMOKE=<0|1>           default: 1
  SELFHOST_STRICT_PROBE_REQUIRE_RUNNABLE=<0|1>     default: 0
  SELFHOST_STRICT_PROBE_STAGE1_PROBE_REQUIRED=<0|1> default: same as REQUIRE_RUNNABLE
  SELFHOST_STRICT_PROBE_MULTI=<0|1>                default: 0
  SELFHOST_STRICT_PROBE_MULTI_FORCE=<0|1>          default: 0
  SELFHOST_STRICT_PROBE_JOBS=<N>                   default: 0 (auto)
  SELFHOST_STRICT_PROBE_INCREMENTAL=<0|1>          default: 1
  SELFHOST_STRICT_PROBE_ALLOW_RETRY=<0|1>          default: 0
  SELFHOST_STRICT_PROBE_TIMING=<path>              optional timing file override
  SELFHOST_STRICT_PROBE_OUT_DIR=<path>             default: artifacts/backend_selfhost_self_obj/probe_<session>
  SELFHOST_STRICT_PROBE_PREFLIGHT=<0|1>            default: 1
  SELFHOST_STRICT_PROBE_PREFLIGHT_TIMEOUT=<seconds> default: 20
  SELFHOST_STRICT_PROBE_PREFLIGHT_INPUT=<path>     default: tests/cheng/backend/fixtures/return_add.cheng

Notes:
  - Runs strict selfhost bootstrap with reuse disabled, intended as a cold-path
    performance probe.
  - In soft mode (default), failure is reported but does not fail the script.
  - Set SELFHOST_STRICT_PROBE_REQUIRE=1 to make it blocking.
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

run_with_timeout() {
  seconds="$1"
  shift
  perl -e '
    use POSIX qw(setsid WNOHANG);
    my $timeout = shift;
    my $pid = fork();
    if (!defined $pid) { exit 127; }
    if ($pid == 0) {
      setsid();
      exec @ARGV;
      exit 127;
    }
    my $end = time + $timeout;
    while (1) {
      my $res = waitpid($pid, WNOHANG);
      if ($res == $pid) {
        my $status = $?;
        if (($status & 127) != 0) {
          exit(128 + ($status & 127));
        }
        exit($status >> 8);
      }
      if (time >= $end) {
        kill "TERM", -$pid;
        kill "TERM", $pid;
        my $grace_end = time + 1;
        while (time < $grace_end) {
          my $r = waitpid($pid, WNOHANG);
          if ($r == $pid) {
            my $status = $?;
            if (($status & 127) != 0) {
              exit(128 + ($status & 127));
            }
            exit($status >> 8);
          }
          select(undef, undef, undef, 0.1);
        }
        kill "KILL", -$pid;
        kill "KILL", $pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
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

session="${SELFHOST_STRICT_PROBE_SESSION:-prod.strict.noreuse}"
session_safe="$(printf '%s' "$session" | tr -c 'A-Za-z0-9._-' '_')"
timeout="${SELFHOST_STRICT_PROBE_TIMEOUT:-60}"
require_probe="${SELFHOST_STRICT_PROBE_REQUIRE:-0}"
stage0="${SELFHOST_STRICT_PROBE_STAGE0:-}"
reuse="${SELFHOST_STRICT_PROBE_REUSE:-0}"
strict_allow_fast_reuse="${SELFHOST_STRICT_PROBE_STRICT_ALLOW_FAST_REUSE:-0}"
skip_sem="${SELFHOST_STRICT_PROBE_SKIP_SEM:-1}"
skip_own="${SELFHOST_STRICT_PROBE_SKIP_OWNERSHIP:-1}"
skip_cprofile="${SELFHOST_STRICT_PROBE_SKIP_CPROFILE:-1}"
generic_mode="${SELFHOST_STRICT_PROBE_GENERIC_MODE:-dict}"
generic_budget="${SELFHOST_STRICT_PROBE_GENERIC_SPEC_BUDGET:-0}"
probe_validate="${SELFHOST_STRICT_PROBE_VALIDATE:-0}"
probe_skip_smoke="${SELFHOST_STRICT_PROBE_SKIP_SMOKE:-1}"
probe_require_runnable="${SELFHOST_STRICT_PROBE_REQUIRE_RUNNABLE:-0}"
probe_stage1_probe_required="${SELFHOST_STRICT_PROBE_STAGE1_PROBE_REQUIRED:-$probe_require_runnable}"
probe_multi="${SELFHOST_STRICT_PROBE_MULTI:-0}"
probe_multi_force="${SELFHOST_STRICT_PROBE_MULTI_FORCE:-0}"
probe_jobs="${SELFHOST_STRICT_PROBE_JOBS:-0}"
probe_incremental="${SELFHOST_STRICT_PROBE_INCREMENTAL:-1}"
probe_allow_retry="${SELFHOST_STRICT_PROBE_ALLOW_RETRY:-0}"
timing_file="${SELFHOST_STRICT_PROBE_TIMING:-artifacts/backend_selfhost_self_obj/selfhost_timing_${session_safe}.tsv}"
probe_out_dir="${SELFHOST_STRICT_PROBE_OUT_DIR:-artifacts/backend_selfhost_self_obj/probe_${session_safe}}"
preflight="${SELFHOST_STRICT_PROBE_PREFLIGHT:-1}"
preflight_timeout="${SELFHOST_STRICT_PROBE_PREFLIGHT_TIMEOUT:-20}"
preflight_input="${SELFHOST_STRICT_PROBE_PREFLIGHT_INPUT:-tests/cheng/backend/fixtures/return_add.cheng}"

echo "== backend.selfhost_strict_noreuse_probe =="
echo "[selfhost_strict_probe] session=$session timeout=${timeout}s mode=strict reuse=$reuse"
echo "[selfhost_strict_probe] profile frontend=stage1 validate=$probe_validate skip_smoke=$probe_skip_smoke require_runnable=$probe_require_runnable stage1_probe=$probe_stage1_probe_required"

if is_true "$preflight" && [ "$stage0" != "" ]; then
  mkdir -p chengcache artifacts/backend_selfhost_self_obj
  preflight_obj="chengcache/.selfhost_strict_probe_preflight_${session_safe}.o"
  preflight_log="artifacts/backend_selfhost_self_obj/selfhost_strict_preflight_${session_safe}.log"
  rm -f "$preflight_obj" "$preflight_log"
  if [ ! -f "$preflight_input" ]; then
    preflight_input="src/backend/tooling/backend_driver.cheng"
  fi
  set +e
  run_with_timeout "$preflight_timeout" env \
    BACKEND_EMIT=obj \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$preflight_input" \
    BACKEND_OUTPUT="$preflight_obj" \
    "$stage0" >"$preflight_log" 2>&1
  preflight_rc="$?"
  set -e
  if [ "$preflight_rc" -ne 0 ]; then
    if [ "$preflight_rc" -eq 124 ] || \
       grep -E -q 'dot base is not an object/ref|unresolved type dependencies|stage1 errors|Unexpected token|parse error|parser error' "$preflight_log"; then
      echo "[selfhost_strict_probe] preflight: stage0 cannot compile preflight input (stage0=$stage0, input=$preflight_input, status=$preflight_rc, log=$preflight_log)" 1>&2
      if is_true "$require_probe"; then
        echo "[selfhost_strict_probe] required probe failed (preflight)" 1>&2
        exit 1
      fi
      echo "[selfhost_strict_probe] soft probe skip due to stage0 preflight incompatibility" 1>&2
      exit 0
    fi
  fi
fi

if [ "$stage0" != "" ]; then
  set +e
  env \
    SELF_OBJ_BOOTSTRAP_MODE=strict \
    SELF_OBJ_BOOTSTRAP_TIMEOUT="$timeout" \
    SELF_OBJ_BOOTSTRAP_SESSION="$session" \
    SELF_OBJ_BOOTSTRAP_REUSE="$reuse" \
    SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" \
    STAGE1_SKIP_SEM="$skip_sem" \
    STAGE1_SKIP_OWNERSHIP="$skip_own" \
    STAGE1_SKIP_CPROFILE="$skip_cprofile" \
    GENERIC_MODE="$generic_mode" \
    GENERIC_SPEC_BUDGET="$generic_budget" \
    SELF_OBJ_BOOTSTRAP_VALIDATE="$probe_validate" \
    SELF_OBJ_BOOTSTRAP_SKIP_SMOKE="$probe_skip_smoke" \
    SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE="$probe_require_runnable" \
    SELF_OBJ_BOOTSTRAP_STAGE1_PROBE_REQUIRED="$probe_stage1_probe_required" \
    SELF_OBJ_BOOTSTRAP_OUT_DIR="$probe_out_dir" \
    SELF_OBJ_BOOTSTRAP_MULTI="$probe_multi" \
    SELF_OBJ_BOOTSTRAP_MULTI_FORCE="$probe_multi_force" \
    SELF_OBJ_BOOTSTRAP_JOBS="$probe_jobs" \
    SELF_OBJ_BOOTSTRAP_INCREMENTAL="$probe_incremental" \
    SELF_OBJ_BOOTSTRAP_ALLOW_RETRY="$probe_allow_retry" \
    SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 \
    SELF_OBJ_BOOTSTRAP_STAGE0="$stage0" \
    sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  rc="$?"
  set -e
else
  set +e
  env \
    SELF_OBJ_BOOTSTRAP_MODE=strict \
    SELF_OBJ_BOOTSTRAP_TIMEOUT="$timeout" \
    SELF_OBJ_BOOTSTRAP_SESSION="$session" \
    SELF_OBJ_BOOTSTRAP_REUSE="$reuse" \
    SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$strict_allow_fast_reuse" \
    STAGE1_SKIP_SEM="$skip_sem" \
    STAGE1_SKIP_OWNERSHIP="$skip_own" \
    STAGE1_SKIP_CPROFILE="$skip_cprofile" \
    GENERIC_MODE="$generic_mode" \
    GENERIC_SPEC_BUDGET="$generic_budget" \
    SELF_OBJ_BOOTSTRAP_VALIDATE="$probe_validate" \
    SELF_OBJ_BOOTSTRAP_SKIP_SMOKE="$probe_skip_smoke" \
    SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE="$probe_require_runnable" \
    SELF_OBJ_BOOTSTRAP_STAGE1_PROBE_REQUIRED="$probe_stage1_probe_required" \
    SELF_OBJ_BOOTSTRAP_OUT_DIR="$probe_out_dir" \
    SELF_OBJ_BOOTSTRAP_MULTI="$probe_multi" \
    SELF_OBJ_BOOTSTRAP_MULTI_FORCE="$probe_multi_force" \
    SELF_OBJ_BOOTSTRAP_JOBS="$probe_jobs" \
    SELF_OBJ_BOOTSTRAP_INCREMENTAL="$probe_incremental" \
    SELF_OBJ_BOOTSTRAP_ALLOW_RETRY="$probe_allow_retry" \
    SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 \
    sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh
  rc="$?"
  set -e
fi

probe_timing_file="$probe_out_dir/selfhost_timing_${session_safe}.tsv"
if [ -f "$probe_timing_file" ] && [ "$probe_timing_file" != "$timing_file" ]; then
  mkdir -p "$(dirname "$timing_file")"
  cp "$probe_timing_file" "$timing_file"
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
