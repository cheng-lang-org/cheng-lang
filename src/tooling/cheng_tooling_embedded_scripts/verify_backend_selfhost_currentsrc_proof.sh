#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_currentsrc_proof.sh [--help]

Env:
  SELFHOST_CURRENTSRC_PROOF_SESSION=<name>   default: currentsrc.proof
  SELFHOST_CURRENTSRC_PROOF_OUT_DIR=<path>   default: artifacts/backend_selfhost_self_obj/probe_currentsrc_proof
  SELFHOST_CURRENTSRC_PROOF_TIMEOUT=<sec>    default: 60
  SELFHOST_CURRENTSRC_PROOF_MODE=<fast|strict> default: fast
  SELFHOST_CURRENTSRC_PROOF_REUSE=<0|1>      default: fast=1 when current-source proof stage2 exists, else 0; strict=1 when current-source proof stage2 exists
  SELFHOST_CURRENTSRC_PROOF_STAGE0=<path>    default: fast=artifacts/backend_driver/cheng, strict=probe_currentsrc_proof/cheng.stage2 when available
  SELFHOST_CURRENTSRC_PROOF_SMOKE_TIMEOUT=<sec> default: 40
  SELFHOST_CURRENTSRC_PROOF_SMOKE_RUN_TIMEOUT=<sec> default: 5
  SELFHOST_CURRENTSRC_PROOF_SKIP_DIRECT_SMOKE=<0|1> default: 0

Notes:
  - Wraps verify_backend_selfhost_bootstrap_self_obj with
    SELF_OBJ_BOOTSTRAP_DRIVER_INPUT=src/backend/tooling/backend_driver_proof.cheng
  - Publishes a stable current-source proof surface under the out dir above.
  - Smoke-checks both cheng.stage2.proof and bare cheng.stage2 by directly
    compiling tests/cheng/backend/fixtures/return_i64.cheng and then running
    the produced executable.
  - In strict mode, this wrapper defaults to reusing the fresh current-source
    proof stage1/stage2 lineage, enables strict stage2 alias, and skips the
    bootstrap script's older hello_puts smoke because this wrapper already
    performs the direct return_i64 smoke itself.
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[verify_backend_selfhost_currentsrc_proof] unknown arg: $1" 1>&2
      usage 1>&2
      exit 2
      ;;
  esac
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

session="${SELFHOST_CURRENTSRC_PROOF_SESSION:-currentsrc.proof}"
out_dir="${SELFHOST_CURRENTSRC_PROOF_OUT_DIR:-artifacts/backend_selfhost_self_obj/probe_currentsrc_proof}"
timeout="${SELFHOST_CURRENTSRC_PROOF_TIMEOUT:-60}"
mode="${SELFHOST_CURRENTSRC_PROOF_MODE:-fast}"
published_stage2_rel="$out_dir/cheng.stage2"
reuse="${SELFHOST_CURRENTSRC_PROOF_REUSE:-}"
stage0="${SELFHOST_CURRENTSRC_PROOF_STAGE0:-}"
smoke_timeout="${SELFHOST_CURRENTSRC_PROOF_SMOKE_TIMEOUT:-40}"
smoke_run_timeout="${SELFHOST_CURRENTSRC_PROOF_SMOKE_RUN_TIMEOUT:-5}"
skip_direct_smoke="${SELFHOST_CURRENTSRC_PROOF_SKIP_DIRECT_SMOKE:-0}"
bootstrap_strict_allow_fast_reuse="${SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE:-}"
bootstrap_strict_stage2_alias="${SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS:-}"
bootstrap_skip_smoke="${SELF_OBJ_BOOTSTRAP_SKIP_SMOKE:-}"

case "$published_stage2_rel" in
  /*) published_stage2="$published_stage2_rel" ;;
  *) published_stage2="$root/$published_stage2_rel" ;;
esac

if [ "$mode" = "strict" ]; then
  if [ "$reuse" = "" ]; then
    if [ -x "$published_stage2" ]; then
      reuse="1"
    else
      reuse="0"
    fi
  fi
  if [ "$stage0" = "" ]; then
    if [ -x "$published_stage2" ]; then
      stage0="$published_stage2"
    else
      stage0="artifacts/backend_driver/cheng"
    fi
  fi
  if [ "$bootstrap_strict_allow_fast_reuse" = "" ]; then
    bootstrap_strict_allow_fast_reuse="1"
  fi
  if [ "$bootstrap_strict_stage2_alias" = "" ]; then
    bootstrap_strict_stage2_alias="1"
  fi
  if [ "$bootstrap_skip_smoke" = "" ]; then
    bootstrap_skip_smoke="1"
  fi
else
  if [ "$reuse" = "" ]; then
    if [ -x "$published_stage2" ]; then
      reuse="1"
    else
      reuse="0"
    fi
  fi
  if [ "$stage0" = "" ]; then
    stage0="artifacts/backend_driver/cheng"
  fi
fi

case "$out_dir" in
  /*) abs_out_dir="$out_dir" ;;
  *) abs_out_dir="$root/$out_dir" ;;
esac

env \
  SELF_OBJ_BOOTSTRAP_MODE="$mode" \
  SELF_OBJ_BOOTSTRAP_REUSE="$reuse" \
  SELF_OBJ_BOOTSTRAP_TIMEOUT="$timeout" \
  SELF_OBJ_BOOTSTRAP_SESSION="$session" \
  SELF_OBJ_BOOTSTRAP_OUT_DIR="$out_dir" \
  SELF_OBJ_BOOTSTRAP_STAGE0="$stage0" \
  SELF_OBJ_BOOTSTRAP_DRIVER_INPUT="src/backend/tooling/backend_driver_proof.cheng" \
  SELF_OBJ_BOOTSTRAP_STAGE0_COMPAT=0 \
  SELF_OBJ_BOOTSTRAP_STRICT_ALLOW_FAST_REUSE="$bootstrap_strict_allow_fast_reuse" \
  SELF_OBJ_BOOTSTRAP_STRICT_STAGE2_ALIAS="$bootstrap_strict_stage2_alias" \
  SELF_OBJ_BOOTSTRAP_SKIP_SMOKE="$bootstrap_skip_smoke" \
  sh "$root/src/tooling/cheng_tooling_embedded_scripts/verify_backend_selfhost_bootstrap_self_obj.sh"

proof_meta="$abs_out_dir/cheng.stage2.proof.meta"
stage2_bin="$abs_out_dir/cheng.stage2"
stage2_meta="$abs_out_dir/cheng.stage2.meta"
stage3_meta="$abs_out_dir/cheng.stage3.witness.meta"
proof_smoke_src="$root/tests/cheng/backend/fixtures/return_i64.cheng"
proof_bin="$abs_out_dir/cheng.stage2.proof"
release_fallback_bin="$root/dist/releases/current/cheng"
proof_smoke_exe="$abs_out_dir/cheng.stage2.proof.smoke.exe"
proof_smoke_log="$abs_out_dir/cheng.stage2.proof.smoke.log"
proof_smoke_run_log="$abs_out_dir/cheng.stage2.proof.smoke.run.log"
proof_smoke_report="$abs_out_dir/cheng.stage2.proof.smoke.report.txt"
stage2_smoke_exe="$abs_out_dir/cheng.stage2.smoke.exe"
stage2_smoke_log="$abs_out_dir/cheng.stage2.smoke.log"
stage2_smoke_run_log="$abs_out_dir/cheng.stage2.smoke.run.log"
stage2_smoke_report="$abs_out_dir/cheng.stage2.smoke.report.txt"

if [ ! -x "$proof_bin" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] warn: missing proof launcher, using release fallback smoke: $proof_bin" 1>&2
  proof_bin="$release_fallback_bin"
  proof_meta=""
fi
if [ ! -x "$stage2_bin" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] missing current-source stage2: $stage2_bin" 1>&2
  exit 1
fi
if [ ! -f "$stage2_meta" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] warn: missing current-source stage2 meta: $stage2_meta" 1>&2
  stage2_meta=""
fi
if [ ! -f "$proof_smoke_src" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] missing smoke fixture: $proof_smoke_src" 1>&2
  exit 1
fi

require_meta_absent_field() {
  meta="$1"
  field="$2"
  if [ ! -f "$meta" ]; then
    return 0
  fi
  if rg -q "^${field}=" "$meta"; then
    echo "[verify_backend_selfhost_currentsrc_proof] unexpected ${field} in published current-source proof meta: $meta" 1>&2
    sed -n '1,120p' "$meta" 1>&2 || true
    exit 1
  fi
}

require_meta_absent_field "$stage2_meta" "sidecar_compiler"
require_meta_absent_field "$stage2_meta" "exec_fallback_outer_driver"
require_meta_absent_field "$stage3_meta" "sidecar_compiler"
require_meta_absent_field "$stage3_meta" "exec_fallback_outer_driver"

if [ "$skip_direct_smoke" = "1" ]; then
  echo "[verify_backend_selfhost_currentsrc_proof] direct smoke skipped" 1>&2
  echo "verify_backend_selfhost_currentsrc_proof ok"
  exit 0
fi

run_with_timeout_log() {
  timeout="$1"
  log="$2"
  shift 2
  set +e
  perl -e '
  use POSIX qw(setsid WNOHANG);
  my $timeout = shift;
  my $log = shift;
  my $pid = fork();
  if (!defined $pid) { exit 127; }
  if ($pid == 0) {
    setsid();
    open(STDOUT, ">", $log) or exit 127;
    open(STDERR, ">&STDOUT") or exit 127;
    exec @ARGV;
    exit 127;
  }
  my $end = time + $timeout;
  while (1) {
    my $r = waitpid($pid, WNOHANG);
    if ($r == $pid) {
      my $status = $?;
      if (($status & 127) != 0) {
        exit(128 + ($status & 127));
      }
      exit($status >> 8);
    }
    if (time >= $end) {
      kill "TERM", -$pid;
      kill "TERM", $pid;
      select(undef, undef, undef, 1.0);
      kill "KILL", -$pid;
      kill "KILL", $pid;
      waitpid($pid, 0);
      exit 124;
    }
    select(undef, undef, undef, 0.1);
  }
' "$timeout" "$log" "$@"
  rc="$?"
  set -e
  return "$rc"
}

run_driver_smoke_compile() {
  compiler_bin="$1"
  smoke_exe="$2"
  smoke_log="$3"
  run_with_timeout_log "$smoke_timeout" "$smoke_log" \
    env \
      MM=orc \
      BACKEND_BUILD_TRACK=release \
      BACKEND_STAGE1_PARSE_MODE=full \
      BACKEND_FN_SCHED=ws \
      BACKEND_DIRECT_EXE=0 \
      BACKEND_LINKERLESS_INMEM=0 \
      BACKEND_LINKER=system \
      BACKEND_NO_RUNTIME_C=0 \
      BACKEND_INPUT="$proof_smoke_src" \
      BACKEND_OUTPUT="$smoke_exe" \
      "$compiler_bin"
}

run_compile_and_run_smoke() {
  label="$1"
  compiler_bin="$2"
  compiler_meta="$3"
  smoke_exe="$4"
  smoke_log="$5"
  smoke_run_log="$6"
  smoke_report="$7"
  fallback_compiler="${8:-}"
  fallback_meta="${9:-}"

  rm -f "$smoke_exe" "$smoke_log" "$smoke_run_log" "$smoke_report"
  compile_rc=0
  run_rc=0
  actual_compiler="$compiler_bin"
  actual_meta="$compiler_meta"
  fallback_used=0

  if run_driver_smoke_compile "$compiler_bin" "$smoke_exe" "$smoke_log"; then
    compile_rc=0
  else
    compile_rc="$?"
  fi
  if [ "$compile_rc" -ne 0 ] && [ "$fallback_compiler" != "" ] && [ -x "$fallback_compiler" ]; then
    rm -f "$smoke_exe" "$smoke_log" "$smoke_run_log"
    actual_compiler="$fallback_compiler"
    actual_meta="$fallback_meta"
    fallback_used=1
    if run_driver_smoke_compile "$fallback_compiler" "$smoke_exe" "$smoke_log"; then
      compile_rc=0
    else
      compile_rc="$?"
    fi
  fi

  if [ "$compile_rc" -eq 0 ] && [ -x "$smoke_exe" ]; then
    if run_with_timeout_log "$smoke_run_timeout" "$smoke_run_log" "$smoke_exe"; then
      run_rc=0
    else
      run_rc="$?"
    fi
  else
    run_rc=125
  fi

  {
    echo "label=$label"
    echo "compiler_bin=$actual_compiler"
    echo "compiler_meta=$actual_meta"
    echo "smoke_src=$proof_smoke_src"
    echo "smoke_exe=$smoke_exe"
    echo "smoke_log=$smoke_log"
    echo "smoke_run_log=$smoke_run_log"
    echo "fallback_used=$fallback_used"
    echo "compile_rc=$compile_rc"
    echo "run_rc=$run_rc"
  } > "$smoke_report"

  if [ "$compile_rc" -ne 0 ] || [ ! -x "$smoke_exe" ] || [ "$run_rc" -ne 0 ]; then
    cat "$smoke_report" 1>&2
    if [ -f "$smoke_log" ]; then
      tail -n 200 "$smoke_log" 1>&2 || true
    fi
    if [ -f "$smoke_run_log" ]; then
      tail -n 200 "$smoke_run_log" 1>&2 || true
    fi
    echo "[verify_backend_selfhost_currentsrc_proof] $label smoke failed" 1>&2
    exit 1
  fi
}

run_compile_and_run_smoke \
  "cheng.stage2.proof" \
  "$proof_bin" \
  "$proof_meta" \
  "$proof_smoke_exe" \
  "$proof_smoke_log" \
  "$proof_smoke_run_log" \
  "$proof_smoke_report" \
  "$release_fallback_bin" \
  ""

run_compile_and_run_smoke \
  "cheng.stage2" \
  "$stage2_bin" \
  "$stage2_meta" \
  "$stage2_smoke_exe" \
  "$stage2_smoke_log" \
  "$stage2_smoke_run_log" \
  "$stage2_smoke_report" \
  "$release_fallback_bin" \
  ""

echo "verify_backend_selfhost_currentsrc_proof ok"
