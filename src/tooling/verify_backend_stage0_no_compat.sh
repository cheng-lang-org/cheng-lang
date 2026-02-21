#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_backend_stage0_no_compat.sh [--help]

Env:
  STAGE0_NO_COMPAT_STAGE0=<path>                 optional stage0 override
  STAGE0_NO_COMPAT_SESSION=<name>                default: prod.stage0_no_compat
  STAGE0_NO_COMPAT_MODE=<fast|strict>            default: fast
  STAGE0_NO_COMPAT_TIMEOUT=<seconds>             default: 60
  STAGE0_NO_COMPAT_REUSE=<0|1>                   default: 0
  STAGE0_NO_COMPAT_VALIDATE=<0|1>                default: 0
  STAGE0_NO_COMPAT_SKIP_SMOKE=<0|1>              default: 1
  STAGE0_NO_COMPAT_REQUIRE_RUNNABLE=<0|1>        default: 0
  STAGE0_NO_COMPAT_STAGE1_PROBE_REQUIRED=<0|1>   default: same as REQUIRE_RUNNABLE
  STAGE0_NO_COMPAT_PROBE_TIMEOUT=<seconds>       default: 20
  STAGE0_NO_COMPAT_INPUT=<path>                  default: tests/cheng/backend/fixtures/return_add.cheng
  STAGE0_NO_COMPAT_OUT_DIR=<path>                default: artifacts/backend_selfhost_self_obj/stage0_no_compat_<session>
  STAGE0_NO_COMPAT_METRICS=<path>                default: <out_dir>/selfhost_metrics_<session>.json

Notes:
  - This gate enforces native stage0->stage1 bootstrap without stage0 compat overlay.
  - It hard-sets SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 and requires retry_stage1_compat=0.
EOF
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

to_abs() {
  p="$1"
  case "$p" in
    /*) printf "%s\n" "$p" ;;
    *) printf "%s/%s\n" "$root" "$p" ;;
  esac
}

driver_stage0_probe_ok() {
  probe_compiler="$1"
  if [ ! -x "$probe_compiler" ]; then
    return 1
  fi
  probe_out="chengcache/.stage0_no_compat_probe_$$.o"
  probe_log="$out_dir/stage0_probe.log"
  rm -f "$probe_out" "$probe_log"
  set +e
  run_with_timeout "$probe_timeout" env \
    BACKEND_EMIT=obj \
    BACKEND_FRONTEND=stage1 \
    BACKEND_INPUT="$probe_input" \
    BACKEND_OUTPUT="$probe_out" \
    "$probe_compiler" >"$probe_log" 2>&1
  probe_rc="$?"
  set -e
  rm -f "$probe_out"
  if [ "$probe_rc" -ne 0 ]; then
    return 1
  fi
  return 0
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[verify_backend_stage0_no_compat] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
  shift || true
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

session="${STAGE0_NO_COMPAT_SESSION:-prod.stage0_no_compat}"
session_safe="$(printf '%s' "$session" | tr -c 'A-Za-z0-9._-' '_')"
mode="${STAGE0_NO_COMPAT_MODE:-fast}"
timeout="${STAGE0_NO_COMPAT_TIMEOUT:-60}"
reuse="${STAGE0_NO_COMPAT_REUSE:-0}"
validate="${STAGE0_NO_COMPAT_VALIDATE:-0}"
skip_smoke="${STAGE0_NO_COMPAT_SKIP_SMOKE:-1}"
require_runnable="${STAGE0_NO_COMPAT_REQUIRE_RUNNABLE:-0}"
stage1_probe_required="${STAGE0_NO_COMPAT_STAGE1_PROBE_REQUIRED:-$require_runnable}"
probe_timeout="${STAGE0_NO_COMPAT_PROBE_TIMEOUT:-20}"
probe_input="${STAGE0_NO_COMPAT_INPUT:-tests/cheng/backend/fixtures/return_add.cheng}"

case "$mode" in
  fast|strict)
    ;;
  *)
    echo "[verify_backend_stage0_no_compat] invalid mode: $mode (expected fast|strict)" 1>&2
    exit 2
    ;;
esac

out_dir_rel="${STAGE0_NO_COMPAT_OUT_DIR:-artifacts/backend_selfhost_self_obj/stage0_no_compat_${session_safe}}"
case "$out_dir_rel" in
  /*)
    out_dir="$out_dir_rel"
    ;;
  *)
    out_dir="$root/$out_dir_rel"
    ;;
esac
mkdir -p "$out_dir" chengcache

metrics_out="${STAGE0_NO_COMPAT_METRICS:-$out_dir/selfhost_metrics_${session_safe}.json}"
rm -f "$metrics_out"

stage0="${STAGE0_NO_COMPAT_STAGE0:-}"
if [ "$stage0" != "" ]; then
  stage0="$(to_abs "$stage0")"
  if ! driver_stage0_probe_ok "$stage0"; then
    echo "[verify_backend_stage0_no_compat] explicit stage0 probe failed: $stage0" 1>&2
    [ -s "$out_dir/stage0_probe.log" ] && echo "[verify_backend_stage0_no_compat] probe log: $out_dir/stage0_probe.log" 1>&2
    exit 1
  fi
else
  stage0=""
  for cand in \
    "${BACKEND_DRIVER:-}" \
    "artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "artifacts/backend_selfhost_self_obj/cheng.stage1" \
    "artifacts/backend_seed/cheng.stage2" \
    "dist/releases/current/cheng" \
    "artifacts/backend_driver/cheng" \
    "./cheng"; do
    [ "$cand" != "" ] || continue
    cand_abs="$(to_abs "$cand")"
    if driver_stage0_probe_ok "$cand_abs"; then
      stage0="$cand_abs"
      break
    fi
  done
  if [ "$stage0" = "" ]; then
    echo "[verify_backend_stage0_no_compat] missing runnable stage0 candidate" 1>&2
    [ -s "$out_dir/stage0_probe.log" ] && echo "[verify_backend_stage0_no_compat] last probe log: $out_dir/stage0_probe.log" 1>&2
    exit 1
  fi
fi

echo "== backend.stage0_no_compat =="
echo "[verify_backend_stage0_no_compat] stage0=$stage0 mode=$mode timeout=${timeout}s"

env \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  SELF_OBJ_BOOTSTRAP_MODE="$mode" \
  SELF_OBJ_BOOTSTRAP_TIMEOUT="$timeout" \
  SELF_OBJ_BOOTSTRAP_SESSION="$session" \
  SELF_OBJ_BOOTSTRAP_OUT_DIR="$out_dir" \
  SELF_OBJ_BOOTSTRAP_METRICS_OUT="$metrics_out" \
  SELF_OBJ_BOOTSTRAP_REUSE="$reuse" \
  SELF_OBJ_BOOTSTRAP_STAGE0="$stage0" \
  SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 \
  SELF_OBJ_BOOTSTRAP_VALIDATE="$validate" \
  SELF_OBJ_BOOTSTRAP_SKIP_SMOKE="$skip_smoke" \
  SELF_OBJ_BOOTSTRAP_REQUIRE_RUNNABLE="$require_runnable" \
  SELF_OBJ_BOOTSTRAP_STAGE1_PROBE_REQUIRED="$stage1_probe_required" \
  SELF_OBJ_BOOTSTRAP_MULTI=0 \
  SELF_OBJ_BOOTSTRAP_MULTI_FORCE=0 \
  SELF_OBJ_BOOTSTRAP_ALLOW_RETRY=0 \
  GENERIC_MODE="${GENERIC_MODE:-dict}" \
  GENERIC_SPEC_BUDGET="${GENERIC_SPEC_BUDGET:-0}" \
  sh src/tooling/verify_backend_selfhost_bootstrap_self_obj.sh

if [ ! -s "$metrics_out" ]; then
  echo "[verify_backend_stage0_no_compat] missing metrics output: $metrics_out" 1>&2
  exit 1
fi

if ! grep -Eq '"retry_stage1_compat"[[:space:]]*:[[:space:]]*0' "$metrics_out"; then
  echo "[verify_backend_stage0_no_compat] stage0 compat fallback was used unexpectedly: $metrics_out" 1>&2
  exit 1
fi

echo "backend.stage0_no_compat ok"
