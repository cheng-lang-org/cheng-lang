#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
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

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
requested_linker="${BACKEND_LINKER:-auto}"

resolve_target() {
  if [ "${BACKEND_TARGET:-}" != "" ] && [ "${BACKEND_TARGET:-}" != "auto" ]; then
    printf '%s\n' "${BACKEND_TARGET}"
    return
  fi
  ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target
}

resolve_link_env() {
  link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:"$requested_linker")"
  resolved_linker=""
  resolved_no_runtime_c="0"
  resolved_runtime_obj=""
  resolved_runtime_obj_assigned="0"
  for entry in $link_env; do
    case "$entry" in
      BACKEND_LINKER=*)
        resolved_linker="${entry#BACKEND_LINKER=}"
        ;;
      BACKEND_NO_RUNTIME_C=*)
        resolved_no_runtime_c="${entry#BACKEND_NO_RUNTIME_C=}"
        ;;
      BACKEND_RUNTIME_OBJ=*)
        resolved_runtime_obj="${entry#BACKEND_RUNTIME_OBJ=}"
        resolved_runtime_obj_assigned="1"
        ;;
    esac
  done
  if [ "$resolved_linker" = "" ]; then
    echo "[Error] verify_backend_concurrency_stress: backend_link_env missing BACKEND_LINKER" 1>&2
    exit 1
  fi
}

if [ "${BACKEND_CONCURRENCY_STRESS_ENABLED:-0}" != "1" ]; then
  echo "verify_backend_concurrency_stress ok (skip: set BACKEND_CONCURRENCY_STRESS_ENABLED=1 to enable)"
  exit 0
fi

n="${BACKEND_CONCURRENCY_N:-50}"
timeout_s="${BACKEND_CONCURRENCY_TIMEOUT:-60}"
out_dir="artifacts/backend_concurrency_stress"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_spawn_chan_i32.cheng"
exe_path="$out_dir/spawn_chan"
target="$(resolve_target)"
resolve_link_env

compile_once() {
  if [ "$resolved_runtime_obj_assigned" = "1" ] && [ "$resolved_no_runtime_c" = "1" ]; then
    run_with_timeout "$timeout_s" env \
      MM=orc \
      "$driver" "$fixture" \
        --frontend:stage1 \
        --emit:exe \
        --target:"$target" \
        --linker:"$resolved_linker" \
        --no-runtime-c \
        --runtime-obj:"$resolved_runtime_obj" \
        --output:"$exe_path"
    return
  fi
  if [ "$resolved_runtime_obj_assigned" = "1" ]; then
    run_with_timeout "$timeout_s" env \
      MM=orc \
      "$driver" "$fixture" \
        --frontend:stage1 \
        --emit:exe \
        --target:"$target" \
        --linker:"$resolved_linker" \
        --runtime-obj:"$resolved_runtime_obj" \
        --output:"$exe_path"
    return
  fi
  if [ "$resolved_no_runtime_c" = "1" ]; then
    run_with_timeout "$timeout_s" env \
      MM=orc \
      "$driver" "$fixture" \
        --frontend:stage1 \
        --emit:exe \
        --target:"$target" \
        --linker:"$resolved_linker" \
        --no-runtime-c \
        --output:"$exe_path"
    return
  fi
  run_with_timeout "$timeout_s" env \
    MM=orc \
    "$driver" "$fixture" \
      --frontend:stage1 \
      --emit:exe \
      --target:"$target" \
      --linker:"$resolved_linker" \
      --output:"$exe_path"
}

set +e
compile_once
status=$?
set -e
if [ "$status" = "124" ]; then
  echo "[Error] verify_backend_concurrency_stress compile timed out after ${timeout_s}s" 1>&2
  exit 124
fi
if [ "$status" != "0" ]; then
  exit "$status"
fi

i=0
while [ "$i" -lt "$n" ]; do
  set +e
  run_with_timeout "$timeout_s" "$exe_path"
  status=$?
  set -e
  if [ "$status" = "124" ]; then
    echo "[Error] verify_backend_concurrency_stress run timed out after ${timeout_s}s (iter=$i)" 1>&2
    exit 124
  fi
  if [ "$status" != "0" ]; then
    exit "$status"
  fi
  i=$((i + 1))
done

echo "verify_backend_concurrency_stress ok (n=$n)"
