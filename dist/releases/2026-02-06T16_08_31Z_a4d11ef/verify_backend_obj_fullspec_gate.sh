#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

run_with_timeout() {
  seconds="$1"
  shift
  case "$seconds" in
    ''|*[!0-9]*)
      "$@"
      return $?
      ;;
  esac
  if command -v timeout >/dev/null 2>&1; then
    timeout "$seconds" "$@"
    return $?
  fi
  if command -v gtimeout >/dev/null 2>&1; then
    gtimeout "$seconds" "$@"
    return $?
  fi
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

sample="examples/backend_obj_fullspec.cheng"
if [ ! -f "$sample" ]; then
  echo "[Error] missing fixed gate sample: $sample" 1>&2
  exit 2
fi

driver="${CHENG_BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[Error] backend driver not executable: $driver" 1>&2
  exit 2
fi

timeout_s="${CHENG_BACKEND_OBJ_FULLSPEC_TIMEOUT:-60}"
out_dir="artifacts/backend_obj_fullspec_gate"
out="$out_dir/backend_obj_fullspec"
log="$out_dir/backend_obj_fullspec.out"
mkdir -p "$out_dir"

echo "== backend.obj_fullspec_gate.build =="
run_with_timeout "$timeout_s" env \
  CHENG_MM="${CHENG_MM:-orc}" \
  CHENG_CLEAN_CHENG_LOCAL="${CHENG_CLEAN_CHENG_LOCAL:-0}" \
  CHENG_BACKEND_DRIVER="$driver" \
  sh src/tooling/chengb.sh "$sample" --frontend:stage1 --emit:exe --out:"$out" >/dev/null

if [ ! -x "$out" ]; then
  echo "[Error] missing gate binary: $out" 1>&2
  exit 1
fi

echo "== backend.obj_fullspec_gate.run =="
run_with_timeout "$timeout_s" "$out" >"$log"
if ! grep -Fq "fullspec ok" "$log"; then
  echo "[Error] gate output missing 'fullspec ok': $log" 1>&2
  exit 1
fi

echo "verify_backend_obj_fullspec_gate ok"
