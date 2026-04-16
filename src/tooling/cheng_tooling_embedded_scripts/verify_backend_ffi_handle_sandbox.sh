#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tooling_self_bin="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling.real.bin}"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    "$tooling_self_bin" cleanup_cheng_local
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
if ! command -v nm >/dev/null 2>&1; then
  fail "nm is required"
fi
if ! command -v ld >/dev/null 2>&1; then
  fail "ld is required"
fi

detect_host_target_shell() {
  host_os="$(uname -s 2>/dev/null || echo unknown)"
  host_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$host_os:$host_arch" in
    Darwin:arm64) echo arm64-apple-darwin ;;
    Darwin:aarch64) echo arm64-apple-darwin ;;
    Darwin:x86_64) echo x86_64-apple-darwin ;;
    Linux:aarch64) echo aarch64-unknown-linux-gnu ;;
    Linux:arm64) echo aarch64-unknown-linux-gnu ;;
    Linux:x86_64) echo x86_64-unknown-linux-gnu ;;
    Linux:riscv64) echo riscv64-unknown-linux-gnu ;;
    *) echo arm64-apple-darwin ;;
  esac
}

resolve_driver() {
  for cand in \
    "${BACKEND_DRIVER:-}" \
    "artifacts/v3_bootstrap/cheng.stage3" \
    "artifacts/v3_backend_driver/cheng" \
    "artifacts/backend_selfhost_self_obj/cheng.stage2" \
    "v2/artifacts/bootstrap/cheng_v2c"; do
    if [ "$cand" != "" ] && [ -x "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

runtime_has_defined_symbol() {
  obj="$1"
  sym="$2"
  [ -f "$obj" ] || return 1
  nm -g "$obj" 2>/dev/null \
    | awk '{
        raw=$0;
        name=$NF;
        gsub(/^_+/, "", name);
        undef=(raw=="U" || raw ~ /^[[:space:]]*U[[:space:]]/ || raw ~ /[[:space:]]U[[:space:]]/);
        if (!undef && name != "") print name;
      }' \
    | grep -Fxq "$sym"
}

runtime_has_required_symbols() {
  obj="$1"
  [ -f "$obj" ] || return 1
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
    if ! runtime_has_defined_symbol "$obj" "$sym"; then
      return 1
    fi
  done
  return 0
}

runtime_has_forbidden_undefineds() {
  obj="$1"
  [ -f "$obj" ] || return 0
  nm -u "$obj" 2>/dev/null | rg -q '[_ ](driver_|uir(Core|Emit))'
}

raw_runtime_has_required_symbols() {
  obj="$1"
  [ -f "$obj" ] || return 1
  for sym in \
    cheng_ffi_raw_new_i32 \
    cheng_ffi_raw_get_i32 \
    cheng_ffi_raw_add_i32 \
    cheng_ffi_raw_release_i32; do
    if ! runtime_has_defined_symbol "$obj" "$sym"; then
      return 1
    fi
  done
  return 0
}

runtime_cc_extra_flags() {
  case "$target" in
    *apple-darwin) printf '%s\n' "-mmacosx-version-min=11.0" ;;
    *) printf '%s\n' "" ;;
  esac
}

runtime_darwin_arch() {
  case "$1" in
    arm64-apple-darwin) printf '%s\n' "arm64" ;;
    x86_64-apple-darwin) printf '%s\n' "x86_64" ;;
    *) return 1 ;;
  esac
}

build_runtime_obj() {
  out="chengcache/runtime_selflink/system_helpers.ffi_handle_bridge.${target}.o"
  build_dir="chengcache/runtime_selflink/.verify_backend_ffi_handle_sandbox.${target}"
  extra_flags="$(runtime_cc_extra_flags)"
  mkdir -p "$build_dir" "$(dirname "$out")"
  src="src/runtime/native/system_helpers_host_process_ffi_bridge.c"
  log="$build_dir/ffi_handle_bridge.log"
  if [ "$extra_flags" != "" ]; then
    # shellcheck disable=SC2086
    "$cc_bin" -std=c11 -O2 $extra_flags -c "$src" -o "$out" >"$log" 2>&1 || {
      sed -n '1,200p' "$log" >&2 || true
      fail "failed to compile ffi handle runtime bridge: $src"
    }
  else
    "$cc_bin" -std=c11 -O2 -c "$src" -o "$out" >"$log" 2>&1 || {
      sed -n '1,200p' "$log" >&2 || true
      fail "failed to compile ffi handle runtime bridge: $src"
    }
  fi
  printf '%s\n' "$out"
}

build_raw_runtime_obj() {
  out="chengcache/runtime_selflink/system_helpers.ffi_raw_bridge.${target}.o"
  build_dir="chengcache/runtime_selflink/.verify_backend_ffi_handle_sandbox.${target}"
  extra_flags="$(runtime_cc_extra_flags)"
  src="src/runtime/native/system_helpers_ffi_raw_bridge.c"
  log="$build_dir/ffi_raw_bridge.log"
  mkdir -p "$build_dir" "$(dirname "$out")"
  if [ "$extra_flags" != "" ]; then
    # shellcheck disable=SC2086
    "$cc_bin" -std=c11 -O2 $extra_flags -c "$src" -o "$out" >"$log" 2>&1 || {
      sed -n '1,200p' "$log" >&2 || true
      fail "failed to compile ffi raw runtime bridge: $src"
    }
  else
    "$cc_bin" -std=c11 -O2 -c "$src" -o "$out" >"$log" 2>&1 || {
      sed -n '1,200p' "$log" >&2 || true
      fail "failed to compile ffi raw runtime bridge: $src"
    }
  fi
  printf '%s\n' "$out"
}

resolve_runtime_obj() {
  if [ "${BACKEND_RUNTIME_OBJ:-}" != "" ]; then
    if runtime_has_required_symbols "${BACKEND_RUNTIME_OBJ}" &&
       ! runtime_has_forbidden_undefineds "${BACKEND_RUNTIME_OBJ}"; then
      printf '%s\n' "${BACKEND_RUNTIME_OBJ}"
      return 0
    fi
    fail "explicit BACKEND_RUNTIME_OBJ is not a minimal ffi runtime bridge: ${BACKEND_RUNTIME_OBJ}"
  fi
  for cand in \
    "chengcache/runtime_selflink/system_helpers.ffi_handle_bridge.${target}.o"; do
    if runtime_has_required_symbols "$cand" &&
       ! runtime_has_forbidden_undefineds "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  built="$(build_runtime_obj)"
  if runtime_has_required_symbols "$built" &&
     ! runtime_has_forbidden_undefineds "$built"; then
    printf '%s\n' "$built"
    return 0
  fi
  fail "built runtime object missing minimal ffi runtime bridge symbols: $built"
}

resolve_raw_runtime_obj() {
  for cand in \
    "chengcache/runtime_selflink/system_helpers.ffi_raw_bridge.${target}.o"; do
    if raw_runtime_has_required_symbols "$cand" &&
       ! runtime_has_forbidden_undefineds "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  built="$(build_raw_runtime_obj)"
  if raw_runtime_has_required_symbols "$built" &&
     ! runtime_has_forbidden_undefineds "$built"; then
    printf '%s\n' "$built"
    return 0
  fi
  fail "built runtime object missing ffi raw bridge symbols: $built"
}

driver="$(resolve_driver || true)"
if [ ! -x "$driver" ]; then
  fail "backend driver not executable: $driver"
fi

target="${BACKEND_TARGET:-$(detect_host_target_shell)}"
gate_linker="${BACKEND_FFI_HANDLE_SANDBOX_LINKER:-system}"
runtime_obj=""
runtime_obj="$(resolve_runtime_obj)"
if [ "$runtime_obj" = "" ] || [ ! -f "$runtime_obj" ]; then
  fail "missing runtime object for probe/runtime surface: $runtime_obj"
fi
raw_runtime_obj=""
raw_runtime_obj="$(resolve_raw_runtime_obj)"
if [ "$raw_runtime_obj" = "" ] || [ ! -f "$raw_runtime_obj" ]; then
  fail "missing raw runtime object for annotated ffi bridge: $raw_runtime_obj"
fi
case "$gate_linker" in
  ""|auto|system)
    link_env="BACKEND_LINKER=system BACKEND_NO_RUNTIME_C=0"
    gate_linker="system"
    ;;
  self)
    link_env="BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1 BACKEND_RUNTIME_OBJ=$runtime_obj"
    ;;
  *)
    fail "invalid BACKEND_FFI_HANDLE_SANDBOX_LINKER: $gate_linker (expected self|system|auto)"
    ;;
esac

compat_header_file="src/runtime/native/system_helpers.h"
backend_runtime_file="src/std/system_helpers_backend.cheng"
fixture_ok="tests/cheng/backend/fixtures/ffi_importc_handle_sandbox_i32.cheng"
fixture_trap="tests/cheng/backend/fixtures/ffi_importc_handle_stale_trap_i32.cheng"
fixture_ann="tests/cheng/backend/fixtures/ffi_importc_handle_annotated_i32.cheng"
fixture_ann_trap="tests/cheng/backend/fixtures/ffi_importc_handle_annotated_stale_trap_i32.cheng"

for runtime_file in "$compat_header_file" "$backend_runtime_file" "$fixture_ok" "$fixture_trap" "$fixture_ann" "$fixture_ann_trap"; do
  if [ ! -f "$runtime_file" ]; then
    fail "missing required file: $runtime_file"
  fi
done

for sym in \
  cheng_ffi_raw_new_i32 \
  cheng_ffi_raw_get_i32 \
  cheng_ffi_raw_add_i32 \
  cheng_ffi_raw_release_i32 \
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
  if ! rg -q "$sym" "$backend_runtime_file"; then
    fail "missing pure cheng runtime symbol: $sym"
  fi
  if ! rg -q "$sym" "$compat_header_file"; then
    fail "missing compat header symbol: $sym"
  fi
  if ! runtime_has_defined_symbol "$runtime_obj" "$sym"; then
    fail "missing runtime object symbol: $sym ($runtime_obj)"
  fi
done

out_dir="artifacts/backend_ffi_handle_sandbox"
mkdir -p "$out_dir"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
probe_src="$out_dir/ffi_handle_sandbox_probe.c"
probe_bin="$out_dir/ffi_handle_sandbox_probe"
fixture_exe="$out_dir/ffi_importc_handle_sandbox_i32.$safe_target"
case "$target" in
  *windows*|*msvc*) fixture_exe="$fixture_exe.exe" ;;
esac
fixture_primary_obj="$fixture_exe.primary.o"
fixture_trap_exe="$out_dir/ffi_importc_handle_stale_trap_i32.$safe_target"
case "$target" in
  *windows*|*msvc*) fixture_trap_exe="$fixture_trap_exe.exe" ;;
esac
fixture_ann_exe="$out_dir/ffi_importc_handle_annotated_i32.$safe_target"
case "$target" in
  *windows*|*msvc*) fixture_ann_exe="$fixture_ann_exe.exe" ;;
esac
fixture_ann_trap_exe="$out_dir/ffi_importc_handle_annotated_stale_trap_i32.$safe_target"
case "$target" in
  *windows*|*msvc*) fixture_ann_trap_exe="$fixture_ann_trap_exe.exe" ;;
esac
probe_compile_log="$out_dir/ffi_handle_sandbox_probe.build.log"
probe_run_log="$out_dir/ffi_handle_sandbox_probe.run.log"
fixture_build_log="$out_dir/ffi_handle_sandbox_fixture.$safe_target.build.log"
fixture_run_log="$out_dir/ffi_handle_sandbox_fixture.$safe_target.run.log"
fixture_trap_build_log="$out_dir/ffi_handle_stale_trap_fixture.$safe_target.build.log"
fixture_trap_run_log="$out_dir/ffi_handle_stale_trap_fixture.$safe_target.run.log"
fixture_ann_build_log="$out_dir/ffi_handle_annotated_fixture.$safe_target.build.log"
fixture_ann_run_log="$out_dir/ffi_handle_annotated_fixture.$safe_target.run.log"
fixture_ann_trap_build_log="$out_dir/ffi_handle_annotated_stale_trap_fixture.$safe_target.build.log"
fixture_ann_trap_run_log="$out_dir/ffi_handle_annotated_stale_trap_fixture.$safe_target.run.log"
fixture_surface_log="$out_dir/ffi_handle_sandbox_fixture.$safe_target.surface.log"
fixture_ann_surface_log="$out_dir/ffi_handle_annotated_fixture.$safe_target.surface.log"
report="$out_dir/backend_ffi_handle_sandbox.$safe_target.report.txt"
snapshot="$out_dir/backend_ffi_handle_sandbox.$safe_target.snapshot.env"
link_input_flag="--link-input:$raw_runtime_obj"

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

rm -f "$probe_bin" "$fixture_exe" "$fixture_exe.o" "$fixture_primary_obj" "$fixture_trap_exe" "$fixture_trap_exe.o" \
  "$fixture_ann_exe" "$fixture_ann_exe.o" "$fixture_ann_trap_exe" "$fixture_ann_trap_exe.o" \
  "$probe_compile_log" "$probe_run_log" "$fixture_build_log" "$fixture_run_log" "$fixture_trap_build_log" \
  "$fixture_trap_run_log" "$fixture_ann_build_log" "$fixture_ann_run_log" "$fixture_ann_trap_build_log" \
  "$fixture_ann_trap_run_log" "$fixture_surface_log" "$fixture_ann_surface_log" "$report" "$snapshot"
rm -rf "${fixture_exe}.objs" "${fixture_exe}.objs.lock" "${fixture_trap_exe}.objs" "${fixture_trap_exe}.objs.lock" \
  "${fixture_ann_exe}.objs" "${fixture_ann_exe}.objs.lock" "${fixture_ann_trap_exe}.objs" "${fixture_ann_trap_exe}.objs.lock"

surface_pattern='void\*|ptr_add\(|load_ptr\(|store_ptr\('
fixture_surface_ok="1"
if rg -n "$surface_pattern" "$fixture_ok" >"$fixture_surface_log"; then
  fixture_surface_ok="0"
  fail "positive fixture leaks raw pointer surface (see $fixture_surface_log)"
fi

fixture_ann_surface_ok="1"
if rg -n "$surface_pattern" "$fixture_ann" >"$fixture_ann_surface_log"; then
  fixture_ann_surface_ok="0"
  fail "annotated fixture leaks raw pointer surface (see $fixture_ann_surface_log)"
fi

fixture_ann_source_ok="1"
for ann_line in \
  '@importc("cheng_ffi_raw_new_i32")' \
  '@importc("cheng_ffi_raw_get_i32")' \
  '@importc("cheng_ffi_raw_add_i32")' \
  '@importc("cheng_ffi_raw_release_i32")'; do
  if ! rg -F -q "$ann_line" "$fixture_ann"; then
    fixture_ann_source_ok="0"
    fail "annotated fixture missing raw ffi declaration: $ann_line"
  fi
done
if [ "$(rg -c '^@ffi_handle$' "$fixture_ann")" -ne 4 ]; then
  fixture_ann_source_ok="0"
  fail "annotated fixture must keep exactly 4 @ffi_handle markers"
fi

extra_ldflags=""
case "$(uname -s 2>/dev/null || echo unknown)" in
  Darwin)
    extra_ldflags="-Wl,-undefined,dynamic_lookup"
    ;;
  Linux)
    extra_ldflags="-Wl,--unresolved-symbols=ignore-all -ldl"
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

set +e
env $link_env \
  STAGE1_NO_POINTERS_NON_C_ABI=1 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 \
  DIAG_CONTEXT=1 \
  "$driver" system-link-exec \
  --root "$root" \
  --in "$fixture_trap" \
  --emit exe \
  --target "$target" \
  --out "$fixture_trap_exe" >"$fixture_trap_build_log" 2>&1
fixture_trap_build_status="$?"
set -e
if [ "$fixture_trap_build_status" -ne 0 ]; then
  sed -n '1,220p' "$fixture_trap_build_log" >&2 || true
  fail "ffi_handle stale trap fixture build failed (status=$fixture_trap_build_status)"
fi
if [ ! -x "$fixture_trap_exe" ]; then
  fail "missing stale trap executable output: $fixture_trap_exe"
fi

fixture_symbol="cheng_ffi_handle_new_i32"
fixture_symbol_check="compiled_handle_runtime_path"
fixture_ann_check="compiled_annotated_runtime_path"

set +e
"$fixture_exe" >"$fixture_run_log" 2>&1
fixture_run_status="$?"
set -e
if [ "$fixture_run_status" -ne 0 ]; then
  sed -n '1,160p' "$fixture_run_log" >&2 || true
  fail "ffi_handle fixture run failed (status=$fixture_run_status)"
fi

set +e
env $link_env \
  STAGE1_NO_POINTERS_NON_C_ABI=1 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 \
  DIAG_CONTEXT=1 \
  "$driver" system-link-exec \
  --root "$root" \
  --in "$fixture_ann" \
  --emit exe \
  --target "$target" \
  "$link_input_flag" \
  --out "$fixture_ann_exe" >"$fixture_ann_build_log" 2>&1
fixture_ann_build_status="$?"
set -e
if [ "$fixture_ann_build_status" -ne 0 ]; then
  sed -n '1,220p' "$fixture_ann_build_log" >&2 || true
  fail "annotated ffi_handle fixture build failed (status=$fixture_ann_build_status)"
fi
if [ ! -x "$fixture_ann_exe" ]; then
  fail "missing annotated executable output: $fixture_ann_exe"
fi

set +e
"$fixture_ann_exe" >"$fixture_ann_run_log" 2>&1
fixture_ann_run_status="$?"
set -e
if [ "$fixture_ann_run_status" -ne 0 ]; then
  sed -n '1,160p' "$fixture_ann_run_log" >&2 || true
  fail "annotated ffi_handle fixture run failed (status=$fixture_ann_run_status)"
fi

set +e
"$fixture_trap_exe" >"$fixture_trap_run_log" 2>&1
fixture_trap_run_status="$?"
set -e
if [ "$fixture_trap_run_status" -eq 0 ]; then
  fail "ffi_handle stale trap fixture unexpectedly exited 0"
fi
if ! rg -q 'reason=ffi_handle|ffi handle invalid:' "$fixture_trap_run_log"; then
  sed -n '1,200p' "$fixture_trap_run_log" >&2 || true
  fail "ffi_handle stale trap fixture missing crash marker"
fi

set +e
env $link_env \
  STAGE1_NO_POINTERS_NON_C_ABI=1 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=1 \
  DIAG_CONTEXT=1 \
  "$driver" system-link-exec \
  --root "$root" \
  --in "$fixture_ann_trap" \
  --emit exe \
  --target "$target" \
  "$link_input_flag" \
  --out "$fixture_ann_trap_exe" >"$fixture_ann_trap_build_log" 2>&1
fixture_ann_trap_build_status="$?"
set -e
if [ "$fixture_ann_trap_build_status" -ne 0 ]; then
  sed -n '1,220p' "$fixture_ann_trap_build_log" >&2 || true
  fail "annotated ffi_handle stale trap fixture build failed (status=$fixture_ann_trap_build_status)"
fi
if [ ! -x "$fixture_ann_trap_exe" ]; then
  fail "missing annotated stale trap executable output: $fixture_ann_trap_exe"
fi

set +e
"$fixture_ann_trap_exe" >"$fixture_ann_trap_run_log" 2>&1
fixture_ann_trap_run_status="$?"
set -e
if [ "$fixture_ann_trap_run_status" -eq 0 ]; then
  fail "annotated ffi_handle stale trap fixture unexpectedly exited 0"
fi
if ! rg -q 'reason=ffi_handle|ffi handle invalid:' "$fixture_ann_trap_run_log"; then
  sed -n '1,200p' "$fixture_ann_trap_run_log" >&2 || true
  fail "annotated ffi_handle stale trap fixture missing crash marker"
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
  echo "raw_runtime_obj=$raw_runtime_obj"
  echo "link_input_flag=$link_input_flag"
  echo "fixture_ok=$fixture_ok"
  echo "probe_src=$probe_src"
  echo "probe_bin=$probe_bin"
  echo "fixture_exe=$fixture_exe"
  echo "fixture_primary_obj=$fixture_primary_obj"
  echo "fixture_trap=$fixture_trap"
  echo "fixture_trap_exe=$fixture_trap_exe"
  echo "fixture_ann_trap=$fixture_ann_trap"
  echo "fixture_ann_exe=$fixture_ann_exe"
  echo "fixture_ann_trap_exe=$fixture_ann_trap_exe"
  echo "fixture_surface_ok=$fixture_surface_ok"
  echo "fixture_ann=$fixture_ann"
  echo "fixture_ann_surface_ok=$fixture_ann_surface_ok"
  echo "fixture_ann_source_ok=$fixture_ann_source_ok"
  echo "probe_compile_status=$probe_compile_status"
  echo "probe_run_status=$probe_run_status"
  echo "fixture_build_status=$fixture_build_status"
  echo "fixture_ann_build_status=$fixture_ann_build_status"
  echo "fixture_ann_check=$fixture_ann_check"
  echo "fixture_run_status=$fixture_run_status"
  echo "fixture_ann_run_status=$fixture_ann_run_status"
  echo "fixture_trap_build_status=$fixture_trap_build_status"
  echo "fixture_trap_run_status=$fixture_trap_run_status"
  echo "fixture_ann_trap_build_status=$fixture_ann_trap_build_status"
  echo "fixture_ann_trap_run_status=$fixture_ann_trap_run_status"
  echo "fixture_symbol=$fixture_symbol"
  echo "fixture_symbol_check=$fixture_symbol_check"
  echo "probe_compile_log=$probe_compile_log"
  echo "probe_run_log=$probe_run_log"
  echo "fixture_build_log=$fixture_build_log"
  echo "fixture_run_log=$fixture_run_log"
  echo "fixture_trap_build_log=$fixture_trap_build_log"
  echo "fixture_trap_run_log=$fixture_trap_run_log"
  echo "fixture_ann_build_log=$fixture_ann_build_log"
  echo "fixture_ann_run_log=$fixture_ann_run_log"
  echo "fixture_ann_trap_build_log=$fixture_ann_trap_build_log"
  echo "fixture_ann_trap_run_log=$fixture_ann_trap_run_log"
  echo "fixture_surface_log=$fixture_surface_log"
  echo "fixture_ann_surface_log=$fixture_ann_surface_log"
} >"$report"

{
  echo "backend_ffi_handle_sandbox_target=$target"
  echo "backend_ffi_handle_sandbox_gate_linker=$gate_linker"
  echo "backend_ffi_handle_sandbox_fixture_surface_ok=$fixture_surface_ok"
  echo "backend_ffi_handle_sandbox_probe_run_status=$probe_run_status"
  echo "backend_ffi_handle_sandbox_fixture_ann_check=$fixture_ann_check"
  echo "backend_ffi_handle_sandbox_fixture_run_status=$fixture_run_status"
  echo "backend_ffi_handle_sandbox_fixture_trap_run_status=$fixture_trap_run_status"
  echo "backend_ffi_handle_sandbox_report=$report"
} >"$snapshot"

echo "verify_backend_ffi_handle_sandbox ok"
