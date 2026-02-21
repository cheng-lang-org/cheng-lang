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

driver_help_ok() {
  cand="$1"
  if [ "$cand" = "" ] || [ ! -x "$cand" ]; then
    return 1
  fi
  set +e
  "$cand" --help >/dev/null 2>&1
  status="$?"
  set -e
  case "$status" in
    0|1|2) return 0 ;;
  esac
  return 1
}

driver_has_non_c_abi_diag() {
  cand="$1"
  if [ "$cand" = "" ] || [ ! -x "$cand" ]; then
    return 1
  fi
  if ! command -v strings >/dev/null 2>&1; then
    return 1
  fi
  tmp_strings="$(mktemp "${TMPDIR:-/tmp}/cheng_driver_strings.XXXXXX" 2>/dev/null || true)"
  if [ "$tmp_strings" = "" ]; then
    return 1
  fi
  set +e
  strings "$cand" 2>/dev/null >"$tmp_strings"
  strings_status="$?"
  grep -Fq "no-pointer policy: pointer types are forbidden outside C ABI modules" "$tmp_strings"
  status="$?"
  set -e
  rm -f "$tmp_strings" 2>/dev/null || true
  if [ "$strings_status" -ne 0 ] && [ "$status" -ne 0 ]; then
    return 1
  fi
  [ "$status" -eq 0 ]
}

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  local_driver="${BACKEND_LOCAL_DRIVER_REL:-artifacts/backend_driver/cheng}"
  if driver_help_ok "$local_driver"; then
    driver="$local_driver"
  else
    driver="$(env \
      ABI=v2_noptr \
      STAGE1_STD_NO_POINTERS=0 \
      STAGE1_STD_NO_POINTERS_STRICT=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      sh src/tooling/backend_driver_path.sh)"
  fi
fi
if [ ! -x "$driver" ]; then
  echo "[Error] backend driver not executable: $driver" 1>&2
  exit 2
fi

target="${BACKEND_TARGET:-}"
if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  target="$(sh src/tooling/detect_host_target.sh)"
fi

out_dir="artifacts/backend_abi_v2_noptr"
probe_dir="src/std"
probe="$probe_dir/abi_v2_noptr_probe_tmp.cheng"
obj="$out_dir/abi_v2_noptr_probe.o"
log="$out_dir/abi_v2_noptr_probe.log"
probe_non_c_abi="tests/cheng/backend/fixtures/abi_v2_noptr_non_c_abi_probe_tmp.cheng"
obj_non_c_abi="$out_dir/abi_v2_noptr_non_c_abi_probe.o"
log_non_c_abi="$out_dir/abi_v2_noptr_non_c_abi_probe.log"
probe_default_cli="tests/cheng/backend/fixtures/abi_v2_noptr_default_cli_probe_tmp.cheng"
obj_default_cli="$out_dir/abi_v2_noptr_default_cli_probe.o"
log_default_cli="$out_dir/abi_v2_noptr_default_cli_probe.log"
bridge_src="src/std/c.cheng"
obj_bridge="$out_dir/abi_v2_noptr_c_bridge_probe.o"
log_bridge="$out_dir/abi_v2_noptr_c_bridge_probe.log"
chengc_tool="src/tooling/chengc.sh"
mkdir -p "$out_dir"
timeout_s="${BACKEND_ABI_V2_NOPTR_TIMEOUT:-60}"
if [ "${GENERIC_MODE:-}" = "" ]; then
  export GENERIC_MODE=dict
fi
if [ "${GENERIC_SPEC_BUDGET:-}" = "" ]; then
  export GENERIC_SPEC_BUDGET=0
fi
gate_generic_mode="${BACKEND_ABI_V2_NOPTR_GENERIC_MODE:-dict}"
gate_generic_budget="${BACKEND_ABI_V2_NOPTR_GENERIC_SPEC_BUDGET:-0}"
if ! driver_has_non_c_abi_diag "$driver"; then
  echo "[Error] driver missing non-C-ABI no-pointer diagnostic marker: $driver" 1>&2
  exit 1
fi

cleanup_probe() {
  rm -f "$probe"
  rm -f "$probe_non_c_abi"
  rm -f "$probe_default_cli"
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

cat >"$probe_default_cli" <<'EOF'
fn main(): int32 =
    var p: int32* = nil
    return 0
EOF

echo "== backend.abi_v2_noptr.v2_noptr =="
set +e
run_with_timeout "$timeout_s" env \
  ABI=v2_noptr \
  STAGE1_STD_NO_POINTERS=1 \
  STAGE1_STD_NO_POINTERS_STRICT=1 \
  STAGE1_NO_POINTERS_NON_C_ABI=1 \
  STAGE1_SKIP_SEM=0 \
  GENERIC_MODE="$gate_generic_mode" \
  GENERIC_SPEC_BUDGET="$gate_generic_budget" \
  STAGE1_SKIP_OWNERSHIP=1 \
  BACKEND_EMIT=obj \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_INPUT="$probe" \
  BACKEND_OUTPUT="$obj" \
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
if ! grep -Fq "std policy: pointer types are forbidden in standard library" "$log" && \
   ! grep -Fq "no-pointer policy: pointer types are forbidden outside C ABI modules" "$log"; then
  echo "[Error] missing expected pointer policy diagnostic in: $log" 1>&2
  exit 1
fi

echo "== backend.abi_v2_noptr.non_c_abi.v2_gate =="
set +e
run_with_timeout "$timeout_s" env \
  ABI=v2_noptr \
  STAGE1_STD_NO_POINTERS=0 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_NO_POINTERS_NON_C_ABI=1 \
  STAGE1_SKIP_SEM=0 \
  GENERIC_MODE="$gate_generic_mode" \
  GENERIC_SPEC_BUDGET="$gate_generic_budget" \
  STAGE1_SKIP_OWNERSHIP=1 \
  BACKEND_EMIT=obj \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_INPUT="$probe_non_c_abi" \
  BACKEND_OUTPUT="$obj_non_c_abi" \
  "$driver" >"$log_non_c_abi" 2>&1
status_non_c_abi="$?"
set -e

if [ "$status_non_c_abi" -eq 0 ]; then
  echo "[Error] expected non-C-ABI probe to be rejected when STAGE1_NO_POINTERS_NON_C_ABI=1: $probe_non_c_abi" 1>&2
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

echo "== backend.abi_v2_noptr.default_cli.non_c_abi =="
set +e
run_with_timeout "$timeout_s" env \
  -u STAGE1_NO_POINTERS_NON_C_ABI \
  -u STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL \
  ABI=v2_noptr \
  STAGE1_STD_NO_POINTERS=1 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_SKIP_SEM=0 \
  STAGE1_SKIP_OWNERSHIP=1 \
  BACKEND_DRIVER="$driver" \
  "$chengc_tool" "$probe_default_cli" --backend:obj --emit-obj --obj-out:"$obj_default_cli" >"$log_default_cli" 2>&1
status_default_cli="$?"
set -e
if [ "$status_default_cli" -eq 124 ]; then
  echo "[Error] default chengc non-C-ABI pointer probe timed out after ${timeout_s}s" 1>&2
  exit 1
fi
if [ "$status_default_cli" -eq 0 ]; then
  echo "[Error] expected default chengc path to reject non-C-ABI pointer usage: $probe_default_cli" 1>&2
  exit 1
fi
if ! grep -Fq "no-pointer policy: pointer types are forbidden outside C ABI modules" "$log_default_cli"; then
  echo "[Error] missing default chengc non-C-ABI pointer diagnostic in: $log_default_cli" 1>&2
  exit 1
fi

echo "== backend.abi_v2_noptr.c_abi_bridge =="
set +e
run_with_timeout "$timeout_s" env \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  ABI=v2_noptr \
  STAGE1_STD_NO_POINTERS=1 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_SKIP_OWNERSHIP=1 \
  BACKEND_ALLOW_NO_MAIN=1 \
  BACKEND_WHOLE_PROGRAM=0 \
  BACKEND_DRIVER="$driver" \
  "$chengc_tool" "$bridge_src" --backend:obj --emit-obj --obj-out:"$obj_bridge" >"$log_bridge" 2>&1
status_bridge="$?"
set -e
if [ "$status_bridge" -ne 0 ]; then
  echo "[Error] expected C ABI bridge probe to pass under v2_noptr: $bridge_src" 1>&2
  if [ "$status_bridge" -eq 124 ]; then
    echo "[Error] C ABI bridge probe timed out after ${timeout_s}s" 1>&2
  fi
  tail -n 120 "$log_bridge" 1>&2 || true
  exit 1
fi
if [ ! -s "$obj_bridge" ]; then
  echo "[Error] missing C ABI bridge object output: $obj_bridge" 1>&2
  exit 1
fi

echo "verify_backend_abi_v2_noptr ok"
