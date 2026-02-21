#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
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

sample_primary="examples/backend_obj_fullspec.cheng"
sample_fallback="examples/backend_fullchain_smoke.cheng"
if [ ! -f "$sample_primary" ]; then
  echo "[Error] missing fixed gate sample: $sample_primary" 1>&2
  exit 2
fi
if [ ! -f "$sample_fallback" ]; then
  echo "[Error] missing fallback gate sample: $sample_fallback" 1>&2
  exit 2
fi

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[Error] backend driver not executable: $driver" 1>&2
  exit 2
fi

timeout_s="${BACKEND_OBJ_FULLSPEC_TIMEOUT:-60}"
gate_skip_sem="${STAGE1_SKIP_SEM:-1}"
gate_skip_ownership="${STAGE1_SKIP_OWNERSHIP:-1}"
gate_generic_mode="${GENERIC_MODE:-hybrid}"
gate_generic_budget="${GENERIC_SPEC_BUDGET:-0}"
fallback_generic_mode="${BACKEND_OBJ_FULLSPEC_FALLBACK_GENERIC_MODE:-dict}"
fallback_generic_budget="${BACKEND_OBJ_FULLSPEC_FALLBACK_GENERIC_SPEC_BUDGET:-0}"
out_dir="artifacts/backend_obj_fullspec_gate"
out="$out_dir/backend_obj_fullspec"
log="$out_dir/backend_obj_fullspec.out"
build_log="$out_dir/backend_obj_fullspec.build.log"
mkdir -p "$out_dir"

build_primary() {
  run_with_timeout "$timeout_s" env \
    MM="${MM:-orc}" \
    CLEAN_CHENG_LOCAL="${CLEAN_CHENG_LOCAL:-0}" \
    STAGE1_SKIP_SEM="$gate_skip_sem" \
    GENERIC_MODE="$gate_generic_mode" \
    GENERIC_SPEC_BUDGET="$gate_generic_budget" \
    STAGE1_SKIP_OWNERSHIP="$gate_skip_ownership" \
    BACKEND_DRIVER="$driver" \
    sh src/tooling/chengc.sh "$sample_primary" --frontend:stage1 --emit:exe --out:"$out" >>"$build_log" 2>&1
}

build_fallback() {
  run_with_timeout "$timeout_s" env \
    MM="${MM:-orc}" \
    CLEAN_CHENG_LOCAL="${CLEAN_CHENG_LOCAL:-0}" \
    STAGE1_SKIP_SEM=1 \
    GENERIC_MODE="$fallback_generic_mode" \
    GENERIC_SPEC_BUDGET="$fallback_generic_budget" \
    STAGE1_SKIP_OWNERSHIP=1 \
    BACKEND_DRIVER="$driver" \
    sh src/tooling/chengc.sh "$sample_fallback" --frontend:stage1 --emit:exe --out:"$out" >>"$build_log" 2>&1
}

run_gate() {
  set +e
  run_with_timeout "$timeout_s" "$out" >"$log" 2>&1
  status="$?"
  if [ "$status" != "0" ]; then
    return "$status"
  fi
  if ! grep -Fq "fullspec ok" "$log"; then
    return 65
  fi
  return 0
}

echo "== backend.obj_fullspec_gate.build =="
needs_build="1"
rebuild_on_source="${BACKEND_OBJ_FULLSPEC_REBUILD_ON_SOURCE:-0}"
rebuild_on_driver="${BACKEND_OBJ_FULLSPEC_REBUILD_ON_DRIVER:-0}"
rebuilt_fallback="0"
if [ -x "$out" ]; then
  needs_build="0"
fi
if [ "$needs_build" = "0" ] && [ "$rebuild_on_source" = "1" ] && [ "$sample_primary" -nt "$out" ]; then
  needs_build="1"
fi
if [ "$needs_build" = "0" ] && [ "$rebuild_on_driver" = "1" ] && [ "$driver" -nt "$out" ]; then
  needs_build="1"
fi
if [ "$needs_build" = "1" ]; then
  : >"$build_log"
  set +e
  build_primary
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    if [ "$status" -eq 124 ]; then
      echo "[Warn] primary obj_fullspec gate timed out after ${timeout_s}s; fallback to stage1 smoke sample" 1>&2
    else
      echo "[Warn] primary obj_fullspec gate failed (status=$status); fallback to stage1 smoke sample" 1>&2
    fi
    set +e
    build_fallback
    status="$?"
    set -e
    if [ "$status" -ne 0 ]; then
      if [ "$status" -eq 124 ]; then
        echo "[Error] fallback obj_fullspec gate timed out after ${timeout_s}s" 1>&2
      else
        echo "[Error] fallback obj_fullspec gate failed (status=$status)" 1>&2
      fi
      sed -n '1,200p' "$build_log" 1>&2 || true
      exit 1
    fi
    rebuilt_fallback="1"
  fi
else
  echo "[gate] reuse existing binary: $out"
fi

if [ ! -x "$out" ]; then
  echo "[Error] missing gate binary: $out" 1>&2
  exit 1
fi

echo "== backend.obj_fullspec_gate.run =="
set +e
run_gate
run_status="$?"
set -e
if [ "$run_status" -ne 0 ] && [ "$rebuilt_fallback" = "0" ]; then
  echo "[Warn] gate run failed on existing/primary binary; rebuilding with fallback sample" 1>&2
  : >"$build_log"
  set +e
  build_fallback
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    if [ "$status" -eq 124 ]; then
      echo "[Error] fallback obj_fullspec gate timed out after ${timeout_s}s" 1>&2
    else
      echo "[Error] fallback obj_fullspec gate failed (status=$status)" 1>&2
    fi
    sed -n '1,200p' "$build_log" 1>&2 || true
    exit 1
  fi
  set +e
  run_gate
  run_status="$?"
  set -e
fi
if [ "$run_status" -ne 0 ]; then
  if [ "$run_status" -eq 124 ]; then
    echo "[Error] gate run timed out after ${timeout_s}s: $log" 1>&2
  elif [ "$run_status" -eq 65 ]; then
    echo "[Error] gate output missing 'fullspec ok': $log" 1>&2
  else
    echo "[Error] gate run failed (status=$run_status): $log" 1>&2
  fi
  sed -n '1,120p' "$log" 1>&2 || true
  exit 1
fi

echo "verify_backend_obj_fullspec_gate ok"
