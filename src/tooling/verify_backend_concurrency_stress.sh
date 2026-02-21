#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
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

driver="$(sh src/tooling/backend_driver_path.sh)"
target="${BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${BACKEND_LINKER:-auto}")"

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

# shellcheck disable=SC2086
set +e
run_with_timeout "$timeout_s" env $link_env \
  MM=orc \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$target" \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$exe_path" \
  "$driver"
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
