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
target="${CHENG_BACKEND_TARGET:-arm64-apple-darwin}"
link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${CHENG_BACKEND_LINKER:-auto}")"


n="${CHENG_BACKEND_STRESS_N:-10}"
timeout_s="${CHENG_BACKEND_STRESS_TIMEOUT:-60}"
out_dir="artifacts/backend_stress"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
exe_path="$out_dir/stage1_smoke"

# shellcheck disable=SC2086
set +e
run_with_timeout "$timeout_s" env $link_env \
  CHENG_C_SYSTEM=system \
  CHENG_MM=orc \
  CHENG_STAGE1_SKIP_SEM="${CHENG_STAGE1_SKIP_SEM:-1}" \
  CHENG_GENERIC_MODE="${CHENG_BACKEND_STRESS_GENERIC_MODE:-dict}" \
  CHENG_GENERIC_SPEC_BUDGET="${CHENG_BACKEND_STRESS_GENERIC_SPEC_BUDGET:-0}" \
  CHENG_STAGE1_SKIP_OWNERSHIP="${CHENG_STAGE1_SKIP_OWNERSHIP:-1}" \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
status=$?
set -e
if [ "$status" = "124" ]; then
  echo "[Error] verify_backend_stress compile timed out after ${timeout_s}s" 1>&2
  exit 124
fi
if [ "$status" != "0" ]; then
  exit "$status"
fi

i=0
while [ "$i" -lt "$n" ]; do
  set +e
  out="$(run_with_timeout "$timeout_s" "$exe_path" 2>&1)"
  status=$?
  set -e
  if [ "$status" = "124" ]; then
    echo "[Error] verify_backend_stress run timed out after ${timeout_s}s (iter=$i)" 1>&2
    exit 124
  fi
  if [ "$status" != "0" ]; then
    printf "%s\n" "$out" >&2
    exit "$status"
  fi
  if ! printf "%s\n" "$out" | grep -Fq "hello from cheng backend"; then
    echo "[Error] verify_backend_stress output mismatch (iter=$i)" 1>&2
    exit 1
  fi
  i=$((i + 1))
done

echo "verify_backend_stress ok (n=$n)"
