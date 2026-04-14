#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
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

driver="${BACKEND_DRIVER:-}"
if [ "$driver" = "" ]; then
  driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path 2>/dev/null || true)"
fi
if [ "$driver" = "" ] && [ -x "v2/artifacts/bootstrap/cheng_v2c" ]; then
  driver="v2/artifacts/bootstrap/cheng_v2c"
fi
if [ ! -x "$driver" ]; then
  fail "backend driver not executable: $driver"
fi

target="${BACKEND_TARGET:-$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target 2>/dev/null || echo arm64-apple-darwin)}"
gate_linker="${BACKEND_FFI_HANDLE_SANDBOX_LINKER:-system}"
runtime_link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:self)"
runtime_obj=""
for entry in $runtime_link_env; do
  case "$entry" in
    BACKEND_RUNTIME_OBJ=*)
      runtime_obj="${entry#BACKEND_RUNTIME_OBJ=}"
      ;;
  esac
done
if [ "$runtime_obj" = "" ] || [ ! -f "$runtime_obj" ]; then
  fail "missing runtime object for probe/runtime surface: $runtime_obj"
fi
case "$gate_linker" in
  ""|auto|system)
    link_env="BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=0"
    gate_linker="system"
    ;;
  self)
    link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:self)"
    ;;
  *)
    fail "invalid BACKEND_FFI_HANDLE_SANDBOX_LINKER: $gate_linker (expected self|system|auto)"
    ;;
esac

header_file="src/runtime/native/system_helpers.h"
backend_runtime_file="src/std/system_helpers_backend.cheng"
fixture_ok="tests/cheng/backend/fixtures/ffi_importc_handle_annotated_i32.cheng"

for runtime_file in "$header_file" "$backend_runtime_file" "$fixture_ok"; do
  if [ ! -f "$runtime_file" ]; then
    fail "missing required file: $runtime_file"
  fi
done

for sym in \
  cheng_ffi_handle_register_ptr \
  cheng_ffi_handle_resolve_ptr \
  cheng_ffi_handle_invalidate \
  cheng_ffi_handle_new_i32 \
  cheng_ffi_handle_get_i32 \
  cheng_ffi_handle_add_i32 \
  cheng_ffi_handle_release_i32 \
  cheng_ffi_raw_new_i32 \
  cheng_ffi_raw_get_i32 \
  cheng_ffi_raw_add_i32 \
  cheng_ffi_raw_release_i32; do
  if ! rg -q "$sym" "$header_file"; then
    fail "missing header symbol: $sym"
  fi
  if ! rg -q "$sym" "$backend_runtime_file"; then
    fail "missing backend runtime symbol: $sym"
  fi
  if command -v nm >/dev/null 2>&1; then
    if ! nm -g "$runtime_obj" 2>/dev/null | awk '{print $NF}' | sed 's/^_//' | grep -Fxq "$sym"; then
      fail "missing runtime object symbol: $sym ($runtime_obj)"
    fi
  fi
done

out_dir="artifacts/backend_ffi_handle_sandbox"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
probe_src="$out_dir/ffi_handle_sandbox_probe.c"
probe_bin="$out_dir/ffi_handle_sandbox_probe"
fixture_exe="$out_dir/ffi_importc_handle_annotated_i32.$safe_target"
case "$target" in
  *windows*|*msvc*) fixture_exe="$fixture_exe.exe" ;;
esac
fixture_primary_obj="$fixture_exe.primary.o"
probe_compile_log="$out_dir/ffi_handle_sandbox_probe.build.log"
probe_run_log="$out_dir/ffi_handle_sandbox_probe.run.log"
fixture_build_log="$out_dir/ffi_handle_sandbox_fixture.$safe_target.build.log"
fixture_run_log="$out_dir/ffi_handle_sandbox_fixture.$safe_target.run.log"
fixture_surface_log="$out_dir/ffi_handle_sandbox_fixture.$safe_target.surface.log"
report="$out_dir/backend_ffi_handle_sandbox.$safe_target.report.txt"
snapshot="$out_dir/backend_ffi_handle_sandbox.$safe_target.snapshot.env"

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
    if (h1 == h0) {
        return 9;
    }
    if (cheng_ffi_handle_get_i32(h1, &out) != 0 || out != 7) {
        return 10;
    }
    if (cheng_ffi_handle_get_i32(h0, &out) != -1) {
        return 11;
    }
    if (cheng_ffi_handle_release_i32(h1) != 0) {
        return 12;
    }

    return 0;
}
PROBE_C

rm -f "$probe_bin" "$fixture_exe" "$fixture_exe.o" "$fixture_primary_obj" "$probe_compile_log" "$probe_run_log" \
  "$fixture_build_log" "$fixture_run_log" "$fixture_surface_log" "$report" "$snapshot"
rm -rf "${fixture_exe}.objs" "${fixture_exe}.objs.lock"

surface_pattern='void\*|ptr_add\(|load_ptr\(|store_ptr\('
fixture_surface_ok="1"
if rg -n "$surface_pattern" "$fixture_ok" >"$fixture_surface_log"; then
  fixture_surface_ok="0"
  fail "positive fixture leaks raw pointer surface (see $fixture_surface_log)"
fi

extra_ldflags=""
case "$(uname -s 2>/dev/null || echo unknown)" in
  Linux)
    extra_ldflags="-ldl"
    ;;
esac

set +e
# shellcheck disable=SC2086
"$cc_bin" -std=c11 -O2 -I src/runtime/native \
  "$runtime_obj" \
  "$probe_src" \
  -o "$probe_bin" $extra_ldflags >"$probe_compile_log" 2>&1
probe_compile_status="$?"
set -e
if [ "$probe_compile_status" -ne 0 ]; then
  sed -n '1,200p' "$probe_compile_log" >&2 || true
  fail "probe compile failed (status=$probe_compile_status)"
fi

set +e
"$probe_bin" >"$probe_run_log" 2>&1
probe_run_status="$?"
set -e
if [ "$probe_run_status" -ne 0 ]; then
  sed -n '1,200p' "$probe_run_log" >&2 || true
  fail "probe run failed (status=$probe_run_status)"
fi

set +e
env $link_env \
  STAGE1_NO_POINTERS_NON_C_ABI=1 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 \
  DIAG_CONTEXT=1 \
  "$driver" system-link-exec \
  --root "$root" \
  --in "$fixture_ok" \
  --emit exe \
  --target "$target" \
  --out "$fixture_exe" >"$fixture_build_log" 2>&1
fixture_build_status="$?"
set -e
if [ "$fixture_build_status" -ne 0 ]; then
  sed -n '1,220p' "$fixture_build_log" >&2 || true
  fail "ffi_handle fixture build failed (status=$fixture_build_status)"
fi
if [ ! -x "$fixture_exe" ]; then
  fail "missing executable output: $fixture_exe"
fi
if [ ! -f "$fixture_primary_obj" ]; then
  fail "missing primary object output: $fixture_primary_obj"
fi

fixture_symbol="cheng_ffi_raw_new_i32"
fixture_symbol_check="skip_compile_metadata_only"

fixture_ann_check="0"
if strings "$fixture_primary_obj" | rg -q 'signature_line\.1=@ffi_handle'; then
  fixture_ann_check="1"
else
  fail "missing @ffi_handle annotation metadata in primary object: $fixture_primary_obj"
fi

set +e
"$fixture_exe" >"$fixture_run_log" 2>&1
fixture_run_status="$?"
set -e
if [ "$fixture_run_status" -ne 0 ]; then
  sed -n '1,160p' "$fixture_run_log" >&2 || true
  fail "ffi_handle fixture run failed (status=$fixture_run_status)"
fi

{
  echo "verify_backend_ffi_handle_sandbox report"
  echo "target=$target"
  echo "driver=$driver"
  echo "gate_linker=$gate_linker"
  echo "link_env=$link_env"
  echo "cc=$cc_bin"
  echo "header_file=$header_file"
  echo "backend_runtime_file=$backend_runtime_file"
  echo "runtime_obj=$runtime_obj"
  echo "fixture_ok=$fixture_ok"
  echo "probe_src=$probe_src"
  echo "probe_bin=$probe_bin"
  echo "fixture_exe=$fixture_exe"
  echo "fixture_primary_obj=$fixture_primary_obj"
  echo "fixture_surface_ok=$fixture_surface_ok"
  echo "probe_compile_status=$probe_compile_status"
  echo "probe_run_status=$probe_run_status"
  echo "fixture_build_status=$fixture_build_status"
  echo "fixture_ann_check=$fixture_ann_check"
  echo "fixture_run_status=$fixture_run_status"
  echo "fixture_symbol=$fixture_symbol"
  echo "fixture_symbol_check=$fixture_symbol_check"
  echo "probe_compile_log=$probe_compile_log"
  echo "probe_run_log=$probe_run_log"
  echo "fixture_build_log=$fixture_build_log"
  echo "fixture_run_log=$fixture_run_log"
  echo "fixture_surface_log=$fixture_surface_log"
} >"$report"

{
  echo "backend_ffi_handle_sandbox_target=$target"
  echo "backend_ffi_handle_sandbox_gate_linker=$gate_linker"
  echo "backend_ffi_handle_sandbox_fixture_surface_ok=$fixture_surface_ok"
  echo "backend_ffi_handle_sandbox_probe_run_status=$probe_run_status"
  echo "backend_ffi_handle_sandbox_fixture_ann_check=$fixture_ann_check"
  echo "backend_ffi_handle_sandbox_fixture_run_status=$fixture_run_status"
  echo "backend_ffi_handle_sandbox_report=$report"
} >"$snapshot"

echo "verify_backend_ffi_handle_sandbox ok"
