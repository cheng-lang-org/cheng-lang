#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
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

compile_cli() {
  src="$1"
  out="$2"
  log="$3"
  run_with_timeout "$timeout_s" env \
    BACKEND_DRIVER="$driver" \
    STAGE1_SEM_FIXED_0=0 \
    STAGE1_OWNERSHIP_FIXED_0=0 \
    "$cheng_tool" cheng --in:"$src" --out:"$out" >"$log" 2>&1
}

out_dir="artifacts/backend_noptr_default_cli"
probe_non_c_abi="tests/cheng/backend/fixtures/noptr_default_cli_probe_tmp.cheng"
out_non_c_abi="$out_dir/noptr_default_cli_probe.bin"
log_non_c_abi="$out_dir/noptr_default_cli_probe.log"
positive_src="tests/cheng/backend/fixtures/return_add.cheng"
out_positive="$out_dir/noptr_default_cli_positive.bin"
log_positive="$out_dir/noptr_default_cli_positive.log"
safe_importc_src="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
out_safe_importc="$out_dir/noptr_default_cli_safe_importc.bin"
log_safe_importc="$out_dir/noptr_default_cli_safe_importc.log"
raw_importc_src="tests/cheng/backend/fixtures/compile_fail_ffi_importc_voidptr_surface.cheng"
out_raw_importc="$out_dir/noptr_default_cli_raw_importc.bin"
log_raw_importc="$out_dir/noptr_default_cli_raw_importc.log"
cheng_tool="${TOOLING_CHENG_BIN:-${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}}"
diag_pattern='no-pointer policy: (bare void\* surface is|pointer types are|pointer dereference is|pointer operation is) forbidden (outside C ABI modules|in user modules)'
backend_block_pattern='macho_writer: unsupported machine op'
timeout_s="${BACKEND_NOPTR_DEFAULT_CLI_TIMEOUT:-60}"
positive_backend_blocked="0"
safe_importc_backend_blocked="0"

mkdir -p "$out_dir"

driver="$(sh "$root/src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh" --field:driver 2>/dev/null || true)"
if [ "$driver" = "" ] || [ ! -x "$driver" ]; then
  echo "[Error] missing strict-fresh backend driver: ${driver:-<unset>}" 1>&2
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

echo "== backend.noptr_default_cli.user_raw_ptr =="
set +e
compile_cli "$probe_non_c_abi" "$out_non_c_abi" "$log_non_c_abi"
status_non_c_abi="$?"
set -e
if [ "$status_non_c_abi" -eq 124 ]; then
  echo "[Error] default cheng raw pointer probe timed out after ${timeout_s}s" 1>&2
  exit 1
fi
if [ "$status_non_c_abi" -eq 0 ]; then
  echo "[Error] expected default cheng path to reject user raw pointer usage: $probe_non_c_abi" 1>&2
  exit 1
fi
if ! grep -Eq "$diag_pattern" "$log_non_c_abi"; then
  echo "[Error] missing default cheng user raw pointer diagnostic in: $log_non_c_abi" 1>&2
  exit 1
fi

echo "== backend.noptr_default_cli.positive =="
set +e
compile_cli "$positive_src" "$out_positive" "$log_positive"
status_positive="$?"
set -e
if [ "$status_positive" -ne 0 ]; then
  if grep -Eq "$diag_pattern" "$log_positive"; then
    echo "[Error] positive sample unexpectedly rejected by no-pointer policy: $positive_src" 1>&2
    exit 1
  fi
  if grep -Fq "$backend_block_pattern" "$log_positive"; then
    positive_backend_blocked="1"
  else
    echo "[Error] expected positive sample to compile under default no-pointer policy: $positive_src" 1>&2
    if [ "$status_positive" -eq 124 ]; then
      echo "[Error] positive sample timed out after ${timeout_s}s" 1>&2
    fi
    tail -n 120 "$log_positive" 1>&2 || true
    exit 1
  fi
fi
if [ "$positive_backend_blocked" != "1" ] && [ ! -s "$out_positive" ]; then
  echo "[Error] missing positive executable output: $out_positive" 1>&2
  exit 1
fi

echo "== backend.noptr_default_cli.safe_importc =="
set +e
compile_cli "$safe_importc_src" "$out_safe_importc" "$log_safe_importc"
status_safe_importc="$?"
set -e
if [ "$status_safe_importc" -ne 0 ]; then
  if grep -Eq "$diag_pattern" "$log_safe_importc"; then
    echo "[Error] safe importc sample unexpectedly rejected by no-pointer policy: $safe_importc_src" 1>&2
    exit 1
  fi
  if grep -Fq "$backend_block_pattern" "$log_safe_importc"; then
    safe_importc_backend_blocked="1"
  else
    echo "[Error] expected safe importc sample to compile under default no-pointer policy: $safe_importc_src" 1>&2
    if [ "$status_safe_importc" -eq 124 ]; then
      echo "[Error] safe importc sample timed out after ${timeout_s}s" 1>&2
    fi
    tail -n 120 "$log_safe_importc" 1>&2 || true
    exit 1
  fi
fi
if [ "$safe_importc_backend_blocked" != "1" ] && [ ! -s "$out_safe_importc" ]; then
  echo "[Error] missing safe importc executable output: $out_safe_importc" 1>&2
  exit 1
fi

echo "== backend.noptr_default_cli.raw_importc =="
set +e
compile_cli "$raw_importc_src" "$out_raw_importc" "$log_raw_importc"
status_raw_importc="$?"
set -e
if [ "$status_raw_importc" -eq 124 ]; then
  echo "[Error] raw importc probe timed out after ${timeout_s}s" 1>&2
  exit 1
fi
if [ "$status_raw_importc" -eq 0 ]; then
  echo "[Error] expected default cheng path to reject raw importc pointer surface: $raw_importc_src" 1>&2
  exit 1
fi
if ! grep -Eq "$diag_pattern" "$log_raw_importc"; then
  echo "[Error] missing raw importc no-pointer diagnostic in: $log_raw_importc" 1>&2
  exit 1
fi

if [ "$positive_backend_blocked" = "1" ] || [ "$safe_importc_backend_blocked" = "1" ]; then
  echo "[Warn] default cheng positive compile hit known backend emitter blocker; no-pointer acceptance verified before backend failure" 1>&2
fi

echo "verify_backend_noptr_default_cli ok"
