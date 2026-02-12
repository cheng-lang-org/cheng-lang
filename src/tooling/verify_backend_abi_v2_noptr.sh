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

driver="${CHENG_BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[Error] backend driver not executable: $driver" 1>&2
  exit 2
fi

target="${CHENG_BACKEND_TARGET:-}"
if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  target="$(sh src/tooling/detect_host_target.sh)"
fi

out_dir="artifacts/backend_abi_v2_noptr"
probe_dir="src/std"
probe="$probe_dir/__abi_v2_noptr_probe_tmp.cheng"
obj="$out_dir/abi_v2_noptr_probe.o"
log="$out_dir/abi_v2_noptr_probe.log"
mkdir -p "$out_dir"
timeout_s="${CHENG_BACKEND_ABI_V2_NOPTR_TIMEOUT:-60}"

cleanup_probe() {
  rm -f "$probe"
}
trap cleanup_probe EXIT

cat >"$probe" <<'EOF'
fn main(): int32 =
    var p: int32* = nil
    return 0
EOF

echo "== backend.abi_v2_noptr.v1 =="
run_with_timeout "$timeout_s" env \
  CHENG_ABI=v1 \
  CHENG_STAGE1_STD_NO_POINTERS=0 \
  CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
  CHENG_STAGE1_SKIP_SEM=0 \
  CHENG_STAGE1_SKIP_MONO=1 \
  CHENG_STAGE1_SKIP_OWNERSHIP=1 \
  CHENG_BACKEND_EMIT=obj \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_INPUT="$probe" \
  CHENG_BACKEND_OUTPUT="$obj" \
  "$driver" >/dev/null

echo "== backend.abi_v2_noptr.v2_noptr =="
set +e
run_with_timeout "$timeout_s" env \
  CHENG_ABI=v2_noptr \
  CHENG_STAGE1_STD_NO_POINTERS=1 \
  CHENG_STAGE1_STD_NO_POINTERS_STRICT=1 \
  CHENG_STAGE1_SKIP_SEM=0 \
  CHENG_STAGE1_SKIP_MONO=1 \
  CHENG_STAGE1_SKIP_OWNERSHIP=1 \
  CHENG_BACKEND_EMIT=obj \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_FRONTEND=stage1 \
  CHENG_BACKEND_INPUT="$probe" \
  CHENG_BACKEND_OUTPUT="$obj" \
  "$driver" >"$log" 2>&1
status="$?"
set -e

if [ "$status" -eq 0 ]; then
  echo "[Error] expected v2_noptr to reject pointer usage under src/std: $probe" 1>&2
  exit 1
fi
if [ "$status" -eq 124 ]; then
  echo "[Error] v2_noptr gate timed out after ${timeout_s}s" 1>&2
  exit 1
fi
if ! grep -Fq "std policy: pointer types are forbidden in standard library" "$log"; then
  echo "[Error] missing expected v2_noptr diagnostic in: $log" 1>&2
  exit 1
fi

echo "verify_backend_abi_v2_noptr ok"
