#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

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

fail() {
  echo "[verify_backend_ffi_handle_sandbox] $1" >&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

cc_bin="${CC:-cc}"
if ! command -v "$cc_bin" >/dev/null 2>&1; then
  fail "C compiler not found: $cc_bin"
fi

header_file="src/runtime/native/system_helpers.h"
c_runtime_file="src/runtime/native/system_helpers.c"
backend_runtime_file="src/std/system_helpers_backend.cheng"

for runtime_file in "$header_file" "$c_runtime_file" "$backend_runtime_file"; do
  if [ ! -f "$runtime_file" ]; then
    fail "missing runtime file: $runtime_file"
  fi
done

for sym in \
  cheng_ffi_handle_register_ptr \
  cheng_ffi_handle_resolve_ptr \
  cheng_ffi_handle_invalidate \
  cheng_ffi_handle_new_i32 \
  cheng_ffi_handle_get_i32 \
  cheng_ffi_handle_add_i32 \
  cheng_ffi_handle_release_i32; do
  if ! rg -q "$sym" "$header_file"; then
    fail "missing header symbol: $sym"
  fi
  if ! rg -q "$sym" "$c_runtime_file"; then
    fail "missing C runtime symbol: $sym"
  fi
  if ! rg -q "$sym" "$backend_runtime_file"; then
    fail "missing backend runtime symbol: $sym"
  fi
done

out_dir="artifacts/backend_ffi_handle_sandbox"
mkdir -p "$out_dir"

probe_src="$out_dir/ffi_handle_sandbox_probe.c"
probe_bin="$out_dir/ffi_handle_sandbox_probe"
probe_compile_log="$out_dir/ffi_handle_sandbox_probe.build.log"
probe_run_log="$out_dir/ffi_handle_sandbox_probe.run.log"
report="$out_dir/backend_ffi_handle_sandbox.report.txt"
snapshot="$out_dir/backend_ffi_handle_sandbox.snapshot.env"

cat >"$probe_src" <<'PROBE_C'
#include <stdint.h>
#include "system_helpers.h"

int main(void) {
    uint64_t h0 = cheng_ffi_handle_new_i32(40);
    if (h0 == 0ULL) {
        return 1;
    }

    int32_t out = 0;
    if (cheng_ffi_handle_get_i32(h0, &out) != 0 || out != 40) {
        return 2;
    }
    if (cheng_ffi_handle_add_i32(h0, 2, &out) != 0 || out != 42) {
        return 3;
    }
    if (cheng_ffi_handle_release_i32(h0) != 0) {
        return 4;
    }

    if (cheng_ffi_handle_get_i32(h0, &out) != -1) {
        return 5;
    }
    if (cheng_ffi_handle_add_i32(h0, 1, &out) != -1) {
        return 6;
    }
    if (cheng_ffi_handle_release_i32(h0) != -1) {
        return 7;
    }

    uint64_t h1 = cheng_ffi_handle_new_i32(7);
    if (h1 == 0ULL) {
        return 8;
    }
    if (cheng_ffi_handle_get_i32(h1, &out) != 0 || out != 7) {
        return 9;
    }
    if (cheng_ffi_handle_get_i32(h0, &out) != -1) {
        return 10;
    }
    if (cheng_ffi_handle_release_i32(h1) != 0) {
        return 11;
    }

    return 0;
}
PROBE_C

rm -f "$probe_bin" "$probe_compile_log" "$probe_run_log" "$report" "$snapshot"

extra_ldflags=""
case "$(uname -s 2>/dev/null || echo unknown)" in
  Linux)
    extra_ldflags="-ldl"
    ;;
esac

set +e
# shellcheck disable=SC2086
"$cc_bin" -std=c11 -O2 -I src/runtime/native \
  "$c_runtime_file" \
  "$probe_src" \
  -o "$probe_bin" $extra_ldflags >"$probe_compile_log" 2>&1
compile_status="$?"
set -e
if [ "$compile_status" -ne 0 ]; then
  sed -n '1,200p' "$probe_compile_log" >&2 || true
  fail "probe compile failed (status=$compile_status)"
fi

set +e
"$probe_bin" >"$probe_run_log" 2>&1
run_status="$?"
set -e
if [ "$run_status" -ne 0 ]; then
  sed -n '1,200p' "$probe_run_log" >&2 || true
  fail "probe run failed (status=$run_status)"
fi

{
  echo "verify_backend_ffi_handle_sandbox report"
  echo "cc=$cc_bin"
  echo "header_file=$header_file"
  echo "c_runtime_file=$c_runtime_file"
  echo "backend_runtime_file=$backend_runtime_file"
  echo "probe_src=$probe_src"
  echo "probe_bin=$probe_bin"
  echo "compile_status=$compile_status"
  echo "run_status=$run_status"
  echo "probe_compile_log=$probe_compile_log"
  echo "probe_run_log=$probe_run_log"
} >"$report"

{
  echo "backend_ffi_handle_sandbox_report=$report"
  echo "backend_ffi_handle_sandbox_run_status=$run_status"
} >"$snapshot"

echo "verify_backend_ffi_handle_sandbox ok"
