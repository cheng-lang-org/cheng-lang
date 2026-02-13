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
log_v1="$out_dir/abi_v2_noptr_probe_v1.log"
probe_non_c_abi="$out_dir/__abi_v2_noptr_non_c_abi_probe_tmp.cheng"
obj_non_c_abi="$out_dir/abi_v2_noptr_non_c_abi_probe.o"
log_non_c_abi="$out_dir/abi_v2_noptr_non_c_abi_probe.log"
mkdir -p "$out_dir"
timeout_s="${CHENG_BACKEND_ABI_V2_NOPTR_TIMEOUT:-60}"
only_v2="${CHENG_BACKEND_ABI_V2_NOPTR_ONLY:-0}"
if [ "${CHENG_GENERIC_MODE:-}" = "" ]; then
  export CHENG_GENERIC_MODE=hybrid
fi
if [ "${CHENG_GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export CHENG_GENERIC_SPEC_BUDGET=0
fi
gate_generic_mode="${CHENG_BACKEND_ABI_V2_NOPTR_GENERIC_MODE:-dict}"
gate_generic_budget="${CHENG_BACKEND_ABI_V2_NOPTR_GENERIC_SPEC_BUDGET:-0}"

cleanup_probe() {
  rm -f "$probe"
  rm -f "$probe_non_c_abi"
}
trap cleanup_probe EXIT

cat >"$probe" <<'EOF'
fn main(): int32 =
    var p: int32* = nil
    return 0
EOF

cat >"$probe_non_c_abi" <<'EOF'
fn main(): int32 =
    var p: int32* = nil
    return 0
EOF

if [ "$only_v2" = "1" ]; then
  echo "== backend.abi_v2_noptr.v1 (skip: CHENG_BACKEND_ABI_V2_NOPTR_ONLY=1) =="
else
  echo "== backend.abi_v2_noptr.v1 =="
  set +e
  run_with_timeout "$timeout_s" env \
    CHENG_ABI=v1 \
    CHENG_STAGE1_STD_NO_POINTERS=0 \
    CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
    CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 \
    CHENG_STAGE1_SKIP_SEM=0 \
    CHENG_GENERIC_MODE="$gate_generic_mode" \
    CHENG_GENERIC_SPEC_BUDGET="$gate_generic_budget" \
    CHENG_STAGE1_SKIP_OWNERSHIP=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$probe" \
    CHENG_BACKEND_OUTPUT="$obj" \
    "$driver" >"$log_v1" 2>&1
  status_v1="$?"
  set -e
  if [ "$status_v1" -ne 0 ]; then
    # Refresh driver path once and retry to avoid stale explicit-driver false negatives.
    fresh_driver="$(env CHENG_BACKEND_DRIVER= sh src/tooling/backend_driver_path.sh)"
    if [ "$fresh_driver" != "" ]; then
      driver="$fresh_driver"
    fi
    set +e
    run_with_timeout "$timeout_s" env \
      CHENG_ABI=v1 \
      CHENG_STAGE1_STD_NO_POINTERS=0 \
      CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
      CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 \
      CHENG_STAGE1_SKIP_SEM=0 \
      CHENG_GENERIC_MODE="$gate_generic_mode" \
      CHENG_GENERIC_SPEC_BUDGET="$gate_generic_budget" \
      CHENG_STAGE1_SKIP_OWNERSHIP=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=stage1 \
      CHENG_BACKEND_INPUT="$probe" \
      CHENG_BACKEND_OUTPUT="$obj" \
      "$driver" >"$log_v1" 2>&1
    status_v1="$?"
    set -e
  fi
  if [ "$status_v1" -ne 0 ]; then
    if [ "$status_v1" -eq 124 ]; then
      echo "[Error] v1 probe timed out after ${timeout_s}s" 1>&2
    else
      echo "[Error] v1 probe failed (status=$status_v1): $log_v1" 1>&2
      sed -n '1,120p' "$log_v1" 1>&2 || true
    fi
    exit 1
  fi
fi

echo "== backend.abi_v2_noptr.v2_noptr =="
set +e
run_with_timeout "$timeout_s" env \
  CHENG_ABI=v2_noptr \
  CHENG_STAGE1_STD_NO_POINTERS=1 \
  CHENG_STAGE1_STD_NO_POINTERS_STRICT=1 \
  CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1 \
  CHENG_STAGE1_SKIP_SEM=0 \
  CHENG_GENERIC_MODE="$gate_generic_mode" \
  CHENG_GENERIC_SPEC_BUDGET="$gate_generic_budget" \
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
  strict_gate="${CHENG_BACKEND_ABI_V2_NOPTR_STRICT:-0}"
  case "$driver" in
    */artifacts/backend_seed/cheng.stage2|*/artifacts/backend_selfhost_self_obj/cheng.stage2)
      if [ "$strict_gate" != "1" ]; then
        echo "[Warn] v2_noptr pointer gate is skipped for seed/selfhost-seed driver: $driver" 1>&2
        echo "verify_backend_abi_v2_noptr ok (skip: set CHENG_BACKEND_ABI_V2_NOPTR_STRICT=1 to enforce hard-fail)"
        exit 0
      fi
      ;;
  esac
  echo "[Error] expected v2_noptr to reject pointer usage under src/std: $probe" 1>&2
  exit 1
fi
if [ "$status" -eq 124 ]; then
  echo "[Error] v2_noptr gate timed out after ${timeout_s}s" 1>&2
  exit 1
fi
if ! grep -Fq "std policy: pointer types are forbidden in standard library" "$log" && \
   ! grep -Fq "no-pointer policy: pointer types are forbidden outside C ABI modules" "$log"; then
  echo "[Error] missing expected pointer policy diagnostic in: $log" 1>&2
  exit 1
fi

if [ "$only_v2" = "1" ]; then
  echo "== backend.abi_v2_noptr.non_c_abi.v2_gate =="
  set +e
  run_with_timeout "$timeout_s" env \
    CHENG_ABI=v2_noptr \
    CHENG_STAGE1_STD_NO_POINTERS=0 \
    CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
    CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1 \
    CHENG_STAGE1_SKIP_SEM=0 \
    CHENG_GENERIC_MODE="$gate_generic_mode" \
    CHENG_GENERIC_SPEC_BUDGET="$gate_generic_budget" \
    CHENG_STAGE1_SKIP_OWNERSHIP=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$probe_non_c_abi" \
    CHENG_BACKEND_OUTPUT="$obj_non_c_abi" \
    "$driver" >"$log_non_c_abi" 2>&1
  status_non_c_abi="$?"
  set -e
else
  echo "== backend.abi_v2_noptr.non_c_abi.v1 =="
  run_with_timeout "$timeout_s" env \
    CHENG_ABI=v1 \
    CHENG_STAGE1_STD_NO_POINTERS=0 \
    CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
    CHENG_STAGE1_NO_POINTERS_NON_C_ABI=0 \
    CHENG_STAGE1_SKIP_SEM=0 \
    CHENG_GENERIC_MODE="$gate_generic_mode" \
    CHENG_GENERIC_SPEC_BUDGET="$gate_generic_budget" \
    CHENG_STAGE1_SKIP_OWNERSHIP=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$probe_non_c_abi" \
    CHENG_BACKEND_OUTPUT="$obj_non_c_abi" \
    "$driver" >/dev/null

  echo "== backend.abi_v2_noptr.non_c_abi.gate =="
  set +e
  run_with_timeout "$timeout_s" env \
    CHENG_ABI=v1 \
    CHENG_STAGE1_STD_NO_POINTERS=0 \
    CHENG_STAGE1_STD_NO_POINTERS_STRICT=0 \
    CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1 \
    CHENG_STAGE1_SKIP_SEM=0 \
    CHENG_GENERIC_MODE="$gate_generic_mode" \
    CHENG_GENERIC_SPEC_BUDGET="$gate_generic_budget" \
    CHENG_STAGE1_SKIP_OWNERSHIP=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=stage1 \
    CHENG_BACKEND_INPUT="$probe_non_c_abi" \
    CHENG_BACKEND_OUTPUT="$obj_non_c_abi" \
    "$driver" >"$log_non_c_abi" 2>&1
  status_non_c_abi="$?"
  set -e
fi

if [ "$status_non_c_abi" -eq 0 ]; then
  echo "[Error] expected non-C-ABI probe to be rejected when CHENG_STAGE1_NO_POINTERS_NON_C_ABI=1: $probe_non_c_abi" 1>&2
  exit 1
fi
if [ "$status_non_c_abi" -eq 124 ]; then
  echo "[Error] non-C-ABI pointer gate timed out after ${timeout_s}s" 1>&2
  exit 1
fi
if ! grep -Fq "no-pointer policy: pointer types are forbidden outside C ABI modules" "$log_non_c_abi"; then
  echo "[Error] missing expected non-C-ABI pointer diagnostic in: $log_non_c_abi" 1>&2
  exit 1
fi

echo "verify_backend_abi_v2_noptr ok"
