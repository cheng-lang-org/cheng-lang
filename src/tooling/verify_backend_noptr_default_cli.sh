#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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

out_dir="artifacts/backend_noptr_default_cli"
probe_non_c_abi="tests/cheng/backend/fixtures/noptr_default_cli_probe_tmp.cheng"
obj_non_c_abi="$out_dir/noptr_default_cli_probe.o"
log_non_c_abi="$out_dir/noptr_default_cli_probe.log"
positive_src="tests/cheng/backend/fixtures/return_add.cheng"
obj_positive="$out_dir/noptr_default_cli_positive.o"
log_positive="$out_dir/noptr_default_cli_positive.log"
bridge_src="src/std/c.cheng"
obj_bridge="$out_dir/noptr_default_cli_c_bridge.o"
log_bridge="$out_dir/noptr_default_cli_c_bridge.log"
chengc_tool="src/tooling/chengc.sh"
timeout_s="${BACKEND_NOPTR_DEFAULT_CLI_TIMEOUT:-60}"

mkdir -p "$out_dir"

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh 2>/dev/null || true)"
fi

if ! driver_has_non_c_abi_diag "$driver"; then
  echo "[Error] backend driver missing non-C-ABI no-pointer diagnostic marker: ${driver:-<unset>}" 1>&2
  exit 1
fi

cleanup_probe() {
  rm -f "$probe_non_c_abi"
}
trap cleanup_probe EXIT INT TERM

cat >"$probe_non_c_abi" <<'EOF'
fn main(): int32 =
    var p: int32* = nil
    return 0
EOF

echo "== backend.noptr_default_cli.non_c_abi_default =="
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
  "$chengc_tool" "$probe_non_c_abi" --backend:obj --emit-obj --obj-out:"$obj_non_c_abi" >"$log_non_c_abi" 2>&1
status_non_c_abi="$?"
set -e
if [ "$status_non_c_abi" -eq 124 ]; then
  echo "[Error] default chengc non-C-ABI pointer probe timed out after ${timeout_s}s" 1>&2
  exit 1
fi
if [ "$status_non_c_abi" -eq 0 ]; then
  echo "[Error] expected default chengc path to reject non-C-ABI pointer usage: $probe_non_c_abi" 1>&2
  exit 1
fi
if ! grep -Fq "no-pointer policy: pointer types are forbidden outside C ABI modules" "$log_non_c_abi"; then
  echo "[Error] missing default chengc non-C-ABI pointer diagnostic in: $log_non_c_abi" 1>&2
  exit 1
fi

echo "== backend.noptr_default_cli.positive =="
set +e
run_with_timeout "$timeout_s" env \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  ABI=v2_noptr \
  STAGE1_STD_NO_POINTERS=1 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_SKIP_SEM=0 \
  STAGE1_SKIP_OWNERSHIP=1 \
  BACKEND_DRIVER="$driver" \
  "$chengc_tool" "$positive_src" --backend:obj --emit-obj --obj-out:"$obj_positive" >"$log_positive" 2>&1
status_positive="$?"
set -e
if [ "$status_positive" -ne 0 ]; then
  echo "[Error] expected non-pointer positive sample to compile with non-C-ABI opt-out: $positive_src" 1>&2
  if [ "$status_positive" -eq 124 ]; then
    echo "[Error] positive sample timed out after ${timeout_s}s" 1>&2
  fi
  tail -n 120 "$log_positive" 1>&2 || true
  exit 1
fi
if [ ! -s "$obj_positive" ]; then
  echo "[Error] missing positive object output: $obj_positive" 1>&2
  exit 1
fi

echo "== backend.noptr_default_cli.c_abi_bridge =="
set +e
run_with_timeout "$timeout_s" env \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  ABI=v2_noptr \
  STAGE1_STD_NO_POINTERS=1 \
  STAGE1_STD_NO_POINTERS_STRICT=0 \
  STAGE1_SKIP_SEM=0 \
  STAGE1_SKIP_OWNERSHIP=1 \
  BACKEND_ALLOW_NO_MAIN=1 \
  BACKEND_WHOLE_PROGRAM=0 \
  BACKEND_DRIVER="$driver" \
  "$chengc_tool" "$bridge_src" --backend:obj --emit-obj --obj-out:"$obj_bridge" >"$log_bridge" 2>&1
status_bridge="$?"
set -e
if [ "$status_bridge" -ne 0 ]; then
  echo "[Error] expected C ABI bridge sample to compile: $bridge_src" 1>&2
  if [ "$status_bridge" -eq 124 ]; then
    echo "[Error] C ABI bridge sample timed out after ${timeout_s}s" 1>&2
  fi
  tail -n 120 "$log_bridge" 1>&2 || true
  exit 1
fi
if [ ! -s "$obj_bridge" ]; then
  echo "[Error] missing C ABI bridge object output: $obj_bridge" 1>&2
  exit 1
fi

echo "verify_backend_noptr_default_cli ok"
