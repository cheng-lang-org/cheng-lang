#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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
        select(undef, undef, undef, 0.5);
        kill "KILL", -$pid;
        exit 124;
      }
      select(undef, undef, undef, 0.1);
    }
  ' "$seconds" "$@"
}

if [ "${CHENG_CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"
linker_mode="${CHENG_BACKEND_LINKER:-self}"
target="${CHENG_BACKEND_TARGET:-arm64-apple-darwin}"
frontend="${CHENG_BACKEND_MM_FRONTEND:-mvp}"
mm_container="${CHENG_BACKEND_MM_CONTAINER:-1}"
mm_container_frontend="${CHENG_BACKEND_MM_CONTAINER_FRONTEND:-mvp}"
mm_skip_sem="${CHENG_BACKEND_MM_SKIP_SEM:-${CHENG_STAGE1_SKIP_SEM:-0}}"
mm_skip_ownership="${CHENG_BACKEND_MM_SKIP_OWNERSHIP:-${CHENG_STAGE1_SKIP_OWNERSHIP:-1}}"
mm_generic_mode="${CHENG_BACKEND_MM_GENERIC_MODE:-hybrid}"
mm_generic_budget="${CHENG_BACKEND_MM_GENERIC_SPEC_BUDGET:-0}"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="${CHENG_BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"
if [ "$linker_mode" = "self" ]; then
  mkdir -p chengcache
  if [ ! -f "$runtime_obj" ] || [ "src/std/system_helpers_backend.cheng" -nt "$runtime_obj" ]; then
    env \
      CHENG_MM=orc \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
      CHENG_BACKEND_OUTPUT="$runtime_obj" \
      "$driver" >/dev/null
  fi
fi


out_dir="artifacts/backend_mm"
timeout_s="${CHENG_BACKEND_MM_TIMEOUT:-60}"
mkdir -p "$out_dir"

run_fixture() {
  fixture="$1"
  base="$2"
  fixture_frontend="$3"
  exe_path="$out_dir/$base"

  if [ "$linker_mode" = "self" ]; then
    self_log="$out_dir/${base}.self.log"
    set +e
    run_with_timeout "$timeout_s" env \
      CHENG_MM=orc \
      CHENG_C_SYSTEM=system \
      CHENG_BACKEND_FRONTEND="$fixture_frontend" \
      CHENG_STAGE1_SKIP_SEM="$mm_skip_sem" \
      CHENG_GENERIC_MODE="$mm_generic_mode" \
      CHENG_GENERIC_SPEC_BUDGET="$mm_generic_budget" \
      CHENG_STAGE1_SKIP_OWNERSHIP="$mm_skip_ownership" \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=exe \
      CHENG_BACKEND_LINKER=self \
      CHENG_BACKEND_NO_RUNTIME_C=1 \
      CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_INPUT="$fixture" \
      CHENG_BACKEND_OUTPUT="$exe_path" \
      "$driver" >"$self_log" 2>&1
    self_status="$?"
    if [ "$self_status" -eq 0 ]; then
      run_with_timeout "$timeout_s" "$exe_path" >>"$self_log" 2>&1
      self_status="$?"
    fi
    set -e
    if [ "$self_status" -eq 124 ]; then
      echo "[Error] verify_backend_mm timed out after ${timeout_s}s: $base (self linker)" >&2
      tail -n 200 "$self_log" >&2 || true
      exit 124
    fi
    if [ "$self_status" -eq 0 ]; then
      return
    fi
    tail -n 200 "$self_log" >&2 || true
    exit "$self_status"
  fi

  run_with_timeout "$timeout_s" env \
    CHENG_MM=orc \
    CHENG_C_SYSTEM=system \
    CHENG_BACKEND_FRONTEND="$fixture_frontend" \
    CHENG_STAGE1_SKIP_SEM="$mm_skip_sem" \
    CHENG_GENERIC_MODE="$mm_generic_mode" \
    CHENG_GENERIC_SPEC_BUDGET="$mm_generic_budget" \
    CHENG_STAGE1_SKIP_OWNERSHIP="$mm_skip_ownership" \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_MULTI=0 \
    CHENG_BACKEND_MULTI_FORCE=0 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_LINKER=system \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver"

  run_with_timeout "$timeout_s" "$exe_path"
}

run_fixture "tests/cheng/backend/fixtures/mm_live_balance.cheng" "mm_live_balance" "$frontend"
if [ "$mm_container" = "1" ]; then
  run_fixture "tests/cheng/backend/fixtures/mm_container_balance.cheng" "mm_container_balance" "$mm_container_frontend"
else
  echo "[verify_backend_mm] skip mm_container_balance (set CHENG_BACKEND_MM_CONTAINER=1 to enable, current=$mm_container)" >&2
fi

echo "verify_backend_mm ok"
