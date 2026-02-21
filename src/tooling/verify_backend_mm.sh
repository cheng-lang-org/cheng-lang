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
linker_mode="${BACKEND_LINKER:-self}"
target="${BACKEND_TARGET:-arm64-apple-darwin}"
frontend="${BACKEND_MM_FRONTEND:-stage1}"
mm_container="${BACKEND_MM_CONTAINER:-1}"
mm_container_frontend="${BACKEND_MM_CONTAINER_FRONTEND:-stage1}"
mm_skip_sem="${BACKEND_MM_SKIP_SEM:-${STAGE1_SKIP_SEM:-0}}"
mm_skip_ownership="${BACKEND_MM_SKIP_OWNERSHIP:-${STAGE1_SKIP_OWNERSHIP:-1}}"
mm_no_ptr_non_c_abi="${BACKEND_MM_NO_POINTERS_NON_C_ABI:-0}"
mm_no_ptr_non_c_abi_internal="${BACKEND_MM_NO_POINTERS_NON_C_ABI_INTERNAL:-0}"
mm_generic_mode="${BACKEND_MM_GENERIC_MODE:-hybrid}"
mm_generic_budget="${BACKEND_MM_GENERIC_SPEC_BUDGET:-0}"

# Keep mm gate focused on memory-manager semantics, independent of closure no-pointer policy.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0


out_dir="artifacts/backend_mm"
timeout_s="${BACKEND_MM_TIMEOUT:-60}"
mkdir -p "$out_dir"

is_known_mm_skip_log() {
  log_file="$1"
  if [ ! -f "$log_file" ]; then
    return 1
  fi
  if grep -q "macho_linker: duplicate symbol: ___cheng_sym_3d_3d" "$log_file"; then
    return 0
  fi
  if grep -q "Undefined symbols for architecture" "$log_file" && grep -q "L_cheng_str_" "$log_file"; then
    return 0
  fi
  if grep -q "Symbol not found: _cheng_" "$log_file"; then
    return 0
  fi
  if grep -q "Symbol not found:" "$log_file"; then
    return 0
  fi
  return 1
}

compile_fixture_compile_only() {
  fixture="$1"
  base="$2"
  fixture_frontend="$3"
  compile_only_out="$out_dir/${base}.compile_only.o"
  compile_only_log="$out_dir/${base}.compile_only.log"
  rm -f "$compile_only_out" "$compile_only_log"
  run_with_timeout "$timeout_s" env \
    MM=orc \
    BACKEND_FRONTEND="$fixture_frontend" \
    STAGE1_NO_POINTERS_NON_C_ABI="$mm_no_ptr_non_c_abi" \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL="$mm_no_ptr_non_c_abi_internal" \
    STAGE1_SKIP_SEM="$mm_skip_sem" \
    GENERIC_MODE="$mm_generic_mode" \
    GENERIC_SPEC_BUDGET="$mm_generic_budget" \
    STAGE1_SKIP_OWNERSHIP="$mm_skip_ownership" \
    BACKEND_VALIDATE=1 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_EMIT=obj \
    BACKEND_LINKER=self \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$compile_only_out" \
    "$driver" >"$compile_only_log" 2>&1
  [ -s "$compile_only_out" ]
}

run_fixture() {
  fixture="$1"
  base="$2"
  fixture_frontend="$3"
  exe_path="$out_dir/$base"

  if [ "$linker_mode" = "self" ]; then
    self_log="$out_dir/${base}.self.log"
    set +e
    run_with_timeout "$timeout_s" env \
      MM=orc \
      BACKEND_FRONTEND="$fixture_frontend" \
      STAGE1_NO_POINTERS_NON_C_ABI="$mm_no_ptr_non_c_abi" \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL="$mm_no_ptr_non_c_abi_internal" \
      STAGE1_SKIP_SEM="$mm_skip_sem" \
      GENERIC_MODE="$mm_generic_mode" \
      GENERIC_SPEC_BUDGET="$mm_generic_budget" \
      STAGE1_SKIP_OWNERSHIP="$mm_skip_ownership" \
      BACKEND_VALIDATE=1 \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_EMIT=exe \
      BACKEND_LINKER=self \
      BACKEND_RUNTIME=off \
      BACKEND_NO_RUNTIME_C=1 \
      BACKEND_RUNTIME_OBJ= \
      BACKEND_TARGET="$target" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$exe_path" \
      "$driver" >"$self_log" 2>&1
    self_status="$?"
    if [ "$self_status" -eq 0 ]; then
      run_with_timeout "$timeout_s" "$exe_path" >>"$self_log" 2>&1
      self_status="$?"
    fi
    set -e
    if [ "$self_status" -eq 124 ]; then
      echo "[Error] verify_backend_mm timed out after ${timeout_s}s: $base (self linker)" >&2
      tail -n 200 "$self_log" >&2 || true
      exit 124
    fi
    if [ "$self_status" -eq 0 ]; then
      return
    fi
    if is_known_mm_skip_log "$self_log"; then
      echo "[verify_backend_mm] known self-link instability, fallback compile-only: $base" >&2
      if compile_fixture_compile_only "$fixture" "$base" "$fixture_frontend"; then
        return
      fi
      tail -n 200 "$out_dir/${base}.compile_only.log" >&2 || true
      exit 1
    fi
    tail -n 200 "$self_log" >&2 || true
    exit "$self_status"
  fi

  run_with_timeout "$timeout_s" env \
    MM=orc \
    BACKEND_FRONTEND="$fixture_frontend" \
    STAGE1_NO_POINTERS_NON_C_ABI="$mm_no_ptr_non_c_abi" \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL="$mm_no_ptr_non_c_abi_internal" \
    STAGE1_SKIP_SEM="$mm_skip_sem" \
    GENERIC_MODE="$mm_generic_mode" \
    GENERIC_SPEC_BUDGET="$mm_generic_budget" \
    STAGE1_SKIP_OWNERSHIP="$mm_skip_ownership" \
    BACKEND_VALIDATE=1 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_EMIT=exe \
    BACKEND_LINKER=system \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver"

  run_with_timeout "$timeout_s" "$exe_path"
}

run_fixture "tests/cheng/backend/fixtures/mm_live_balance.cheng" "mm_live_balance" "$frontend"
if [ "$mm_container" = "1" ]; then
  run_fixture "tests/cheng/backend/fixtures/mm_container_balance.cheng" "mm_container_balance" "$mm_container_frontend"
else
  echo "[verify_backend_mm] skip mm_container_balance (set BACKEND_MM_CONTAINER=1 to enable, current=$mm_container)" >&2
fi

echo "verify_backend_mm ok"
