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

out_dir="artifacts/backend_spawn_api_gate"
mkdir -p "$out_dir"
timeout_s="${CHENG_BACKEND_SPAWN_GATE_TIMEOUT:-60}"

raw_spawn_pattern="fn[[:space:]]+spawn\\(fn_ptr:[[:space:]]*void\\*,[[:space:]]*ctx:[[:space:]]*void\\*\\)"
find_raw_spawn() {
  path="$1"
  if command -v rg >/dev/null 2>&1; then
    rg -q "$raw_spawn_pattern" "$path"
    return $?
  fi
  grep -Eq "$raw_spawn_pattern" "$path"
}

if find_raw_spawn src/std/async_rt.cheng; then
  echo "[Error] std/async_rt still exposes raw spawn(fn_ptr: void*, ctx: void*)" 1>&2
  exit 1
fi
if ! find_raw_spawn src/std/async_rt_legacy.cheng; then
  echo "[Error] std/async_rt_legacy missing legacy raw spawn(fn_ptr: void*, ctx: void*)" 1>&2
  exit 1
fi

compile_ok() {
  name="$1"
  fixture="$2"
  obj="$3"
  log="$4"
  set +e
  run_with_timeout "$timeout_s" env \
    CHENG_MM=orc \
    CHENG_STAGE1_SKIP_SEM=0 \
    CHENG_STAGE1_SKIP_MONO=1 \
    CHENG_STAGE1_SKIP_OWNERSHIP=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$obj" \
    "$driver" >"$log" 2>&1
  status="$?"
  set -e
  if [ "$status" = "124" ]; then
    echo "[Error] ${name} timed out after ${timeout_s}s" 1>&2
    exit 1
  fi
  if [ "$status" -ge "128" ]; then
    echo "[Error] ${name} crashed (status=$status)" 1>&2
    cat "$log" 1>&2
    exit 1
  fi
  if [ "$status" != "0" ]; then
    echo "[Error] ${name} failed (status=$status)" 1>&2
    cat "$log" 1>&2
    exit 1
  fi
}

compile_fail() {
  name="$1"
  fixture="$2"
  obj="$3"
  log="$4"
  set +e
  run_with_timeout "$timeout_s" env \
    CHENG_MM=orc \
    CHENG_STAGE1_SKIP_SEM=0 \
    CHENG_STAGE1_SKIP_MONO=1 \
    CHENG_STAGE1_SKIP_OWNERSHIP=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$obj" \
    "$driver" >"$log" 2>&1
  status="$?"
  set -e
  if [ "$status" = "0" ]; then
    echo "[Error] ${name} unexpectedly succeeded: $fixture" 1>&2
    exit 1
  fi
  if [ "$status" = "124" ]; then
    echo "[Error] ${name} timed out after ${timeout_s}s" 1>&2
    exit 1
  fi
  if [ "$status" -ge "128" ]; then
    echo "[Error] ${name} crashed (status=$status)" 1>&2
    cat "$log" 1>&2
    exit 1
  fi
  if ! grep -Fq "spawn" "$log"; then
    echo "[Error] ${name} failed without spawn-related diagnostics" 1>&2
    cat "$log" 1>&2
    exit 1
  fi
}

compile_ok \
  "spawn.thread_entry.default" \
  "tests/cheng/backend/fixtures/return_spawn_default_thread_entry_gate.cheng" \
  "$out_dir/return_spawn_default_thread_entry_gate.o" \
  "$out_dir/return_spawn_default_thread_entry_gate.log"

compile_ok \
  "spawn.typed.default" \
  "tests/cheng/backend/fixtures/return_spawn_typed_value_gate.cheng" \
  "$out_dir/return_spawn_typed_value_gate.o" \
  "$out_dir/return_spawn_typed_value_gate.log"

compile_ok \
  "spawn.legacy.explicit" \
  "tests/cheng/backend/fixtures/return_spawn_legacy_namespaced_gate.cheng" \
  "$out_dir/return_spawn_legacy_namespaced_gate.o" \
  "$out_dir/return_spawn_legacy_namespaced_gate.log"

compile_fail \
  "spawn.raw.default_forbidden" \
  "tests/cheng/backend/fixtures/compile_fail_spawn_raw_default_gate.cheng" \
  "$out_dir/compile_fail_spawn_raw_default_gate.o" \
  "$out_dir/compile_fail_spawn_raw_default_gate.log"

echo "verify_backend_spawn_api_gate ok"
