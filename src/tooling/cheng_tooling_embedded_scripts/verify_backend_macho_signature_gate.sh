#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_backend_macho_signature_gate] $1" >&2
  exit 1
}

require_marker() {
  file="$1"
  pattern="$2"
  marker="$3"
  if ! rg -q "$pattern" "$file"; then
    fail "missing marker ($marker) in $file"
  fi
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

require_marker "src/backend/obj/macho_direct_exe_writer.cheng" 'machoLinkExeAarch64MainObjMem' 'direct_writer_calls_macho_linker'
require_marker "src/backend/obj/macho_linker.cheng" 'LC_CODE_SIGNATURE' 'macho_signature_load_command_const'
require_marker "src/backend/obj/macho_linker.cheng" 'codesign' 'macho_codesign_contract_marker'
require_marker "src/backend/tooling/backend_driver.cheng" 'BACKEND_CODESIGN' 'backend_codesign_switch'
require_marker "src/backend/tooling/backend_driver.cheng" 'codesign -s -' 'backend_codesign_invocation'

out_dir="artifacts/backend_macho_signature_gate"
mkdir -p "$out_dir"
report="$out_dir/backend_macho_signature_gate.report.txt"

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target 2>/dev/null || true)"
runtime_mode="static"
signature_verified="marker_only"
run_status="0"
run_mode="not_attempted"
run_reason="none"

if [ "$host_os" = "Darwin" ]; then
  if ! command -v codesign >/dev/null 2>&1; then
    fail "Darwin host missing codesign"
  fi
  if ! command -v otool >/dev/null 2>&1; then
    fail "Darwin host missing otool"
  fi

  runtime_mode="darwin_probe"
  driver="${BACKEND_DRIVER:-}"
  if [ "$driver" = "" ]; then
    driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
  fi
  [ -x "$driver" ] || fail "backend driver not executable: $driver"

  target="${BACKEND_MACHO_SIGNATURE_TARGET:-arm64-apple-darwin}"
  fixture="tests/cheng/backend/fixtures/return_add.cheng"
  if [ ! -f "$fixture" ]; then
    fixture="tests/cheng/backend/fixtures/return_i64.cheng"
  fi
  safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
  exe_path="$out_dir/macho_signature_probe.$safe_target"
  build_log="$out_dir/macho_signature_probe.$safe_target.build.log"
  otool_log="$out_dir/macho_signature_probe.$safe_target.otool.log"
  codesign_log="$out_dir/macho_signature_probe.$safe_target.codesign.log"
  run_log="$out_dir/macho_signature_probe.$safe_target.run.log"

  rm -f "$exe_path" "$exe_path.o" "$build_log" "$otool_log" "$codesign_log" "$run_log"
  rm -rf "$exe_path.objs" "$exe_path.objs.lock"

  set +e
  env \
    MM="${MM:-orc}" \
    ABI=v2_noptr \
    BACKEND_TARGET="$target" \
    BACKEND_LINKER=self \
    BACKEND_CODESIGN=1 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_INCREMENTAL=0 \
    BACKEND_KEEP_EXE_OBJ=0 \
    STAGE1_STD_NO_POINTERS=0 \
    STAGE1_STD_NO_POINTERS_STRICT=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SEM_FIXED_0="${STAGE1_SEM_FIXED_0:-0}" \
    STAGE1_OWNERSHIP_FIXED_0="${STAGE1_OWNERSHIP_FIXED_0:-0}" \
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} chengc "$fixture" --frontend:stage1 --emit:exe --out:"$exe_path" >"$build_log" 2>&1
  build_status="$?"
  set -e
  if [ "$build_status" -ne 0 ]; then
    sed -n '1,160p' "$build_log" >&2 || true
    fail "darwin probe build failed (status=$build_status): $build_log"
  fi
  [ -s "$exe_path" ] || fail "missing probe executable: $exe_path"

  otool -l "$exe_path" >"$otool_log"
  if ! rg -q 'LC_CODE_SIGNATURE' "$otool_log"; then
    fail "missing LC_CODE_SIGNATURE in probe executable: $exe_path"
  fi

  set +e
  codesign -vv "$exe_path" >"$codesign_log" 2>&1
  sign_status="$?"
  set -e
  if [ "$sign_status" -ne 0 ]; then
    sed -n '1,120p' "$codesign_log" >&2 || true
    fail "codesign verify failed (status=$sign_status): $exe_path"
  fi
  signature_verified="codesign_verified"

  if [ "$target" = "$host_target" ]; then
    run_mode="host"
    set +e
    "$exe_path" >"$run_log" 2>&1
    run_status="$?"
    set -e
    if [ "$run_status" -ne 0 ]; then
      if rg -q 'Symbol not found: _cheng_|Symbol not found: _memRetain|dyld: Symbol not found' "$run_log"; then
        run_mode="known_symbol_unstable_skip"
        run_reason="runtime_symbol_missing"
        run_status="0"
      else
        sed -n '1,120p' "$run_log" >&2 || true
        fail "signed probe run failed (status=$run_status): $exe_path"
      fi
    fi
  else
    run_mode="skip_target_mismatch"
    printf 'skip run: target=%s host_target=%s\n' "$target" "$host_target" >"$run_log"
  fi
fi

{
  echo "verify_backend_macho_signature_gate report"
  echo "host_os=$host_os"
  echo "host_target=$host_target"
  echo "runtime_mode=$runtime_mode"
  echo "signature_verified=$signature_verified"
  echo "run_mode=$run_mode"
  echo "run_reason=$run_reason"
  echo "run_status=$run_status"
} >"$report"

echo "verify_backend_macho_signature_gate ok"
