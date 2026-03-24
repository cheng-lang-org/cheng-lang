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
  echo "[verify_backend_import_cycle_predeclare] $1" >&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target 2>/dev/null || echo arm64-apple-darwin)"
requested_linker="system"

print_usage() {
  echo "Usage: $0 [--driver:<path>] [--target:<triple>] [--linker:self|system|auto]"
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --driver:*)
      driver="${1#--driver:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --linker:*)
      requested_linker="${1#--linker:}"
      ;;
    --help|-h)
      print_usage
      exit 0
      ;;
    *)
      fail "unknown arg: $1"
      ;;
  esac
  shift || true
done

if [ ! -x "$driver" ]; then
  fail "backend driver not executable: $driver"
fi
driver_exec="$driver"
driver_real_env=""
if [ -x "src/tooling/backend_driver_exec.sh" ]; then
  driver_exec="src/tooling/backend_driver_exec.sh"
  driver_real_env="BACKEND_DRIVER_REAL=$driver"
fi

safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
case "$requested_linker" in
  self|system|auto)
    ;;
  *)
    fail "invalid linker: $requested_linker"
    ;;
esac
link_env=""
if [ "$requested_linker" = "self" ]; then
  link_env="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:self)"
fi

fixture_ok="tests/cheng/backend/fixtures/return_forward_decl_call.cheng"
fixture_fail="tests/cheng/backend/fixtures/compile_fail_import_cycle_entry.cheng"
fixture_cycle_a="tests/cheng/backend/fixtures/import_cycle_a.cheng"
fixture_cycle_b="tests/cheng/backend/fixtures/import_cycle_b.cheng"
for f in "$fixture_ok" "$fixture_fail" "$fixture_cycle_a" "$fixture_cycle_b"; do
  if [ ! -f "$f" ]; then
    fail "missing fixture: $f"
  fi
done

out_dir="artifacts/backend_import_cycle_predeclare"
mkdir -p "$out_dir"

ok_exe="$out_dir/return_forward_decl_call.$safe_target"
fail_exe="$out_dir/compile_fail_import_cycle_entry.$safe_target"
case "$target" in
  *windows*|*msvc*)
    ok_exe="$ok_exe.exe"
    fail_exe="$fail_exe.exe"
    ;;
esac

build_ok_log="$out_dir/forward_decl.$safe_target.build.log"
run_ok_log="$out_dir/forward_decl.$safe_target.run.log"
build_fail_log="$out_dir/import_cycle.$safe_target.build.log"
report="$out_dir/backend_import_cycle_predeclare.$safe_target.report.txt"
snapshot="$out_dir/backend_import_cycle_predeclare.$safe_target.snapshot.env"

rm -f "$ok_exe" "$fail_exe" "$build_ok_log" "$run_ok_log" "$build_fail_log" "$report" "$snapshot"
rm -rf "${ok_exe}.objs" "${ok_exe}.objs.lock" "${fail_exe}.objs" "${fail_exe}.objs.lock"

build_one() {
  fixture="$1"
  out="$2"
  log="$3"
  # shellcheck disable=SC2086
  env \
    $link_env \
    $driver_real_env \
    MM=orc \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SEM_FIXED_0=0 \
    STAGE1_OWNERSHIP_FIXED_0=0 \
    GENERIC_MODE=dict \
    GENERIC_SPEC_BUDGET=0 \
    "$driver_exec" "$fixture" \
    --emit:exe \
    --target:"$target" \
    --frontend:stage1 \
    --linker:"$requested_linker" \
    --no-multi \
    --no-multi-force \
    --output:"$out" >"$log" 2>&1
}

set +e
build_one "$fixture_ok" "$ok_exe" "$build_ok_log"
build_ok_status="$?"
set -e
if [ "$build_ok_status" -ne 0 ]; then
  sed -n '1,220p' "$build_ok_log" >&2 || true
  fail "forward declaration fixture build failed (status=$build_ok_status)"
fi
if [ ! -x "$ok_exe" ]; then
  fail "missing executable output: $ok_exe"
fi

set +e
"$ok_exe" >"$run_ok_log" 2>&1
run_ok_status="$?"
set -e
if [ "$run_ok_status" -ne 0 ]; then
  sed -n '1,120p' "$run_ok_log" >&2 || true
  fail "forward declaration fixture run failed (status=$run_ok_status)"
fi

set +e
build_one "$fixture_fail" "$fail_exe" "$build_fail_log"
build_fail_status="$?"
set -e

diag_pattern='Import cycle detected:'
diag_chain_pattern=' -> '
diag_ok="0"
diag_chain_ok="0"
diag_mode="runtime"
if [ "$build_fail_status" -eq 0 ]; then
  sed -n '1,260p' "$build_fail_log" >&2 || true
  fail "import cycle fixture unexpectedly compiled successfully"
fi
if rg -q "$diag_pattern" "$build_fail_log"; then
  diag_ok="1"
fi
if rg -q "$diag_chain_pattern" "$build_fail_log"; then
  diag_chain_ok="1"
fi
if [ "$diag_ok" != "1" ] || [ "$diag_chain_ok" != "1" ]; then
  sed -n '1,260p' "$build_fail_log" >&2 || true
  fail "missing expected import cycle diagnostic"
fi

{
  echo "verify_backend_import_cycle_predeclare report"
  echo "target=$target"
  echo "driver=$driver"
  echo "driver_exec=$driver_exec"
  echo "requested_linker=$requested_linker"
  echo "fixture_ok=$fixture_ok"
  echo "fixture_fail=$fixture_fail"
  echo "fixture_cycle_a=$fixture_cycle_a"
  echo "fixture_cycle_b=$fixture_cycle_b"
  echo "link_env=$link_env"
  echo "build_ok_status=$build_ok_status"
  echo "run_ok_status=$run_ok_status"
  echo "build_fail_status=$build_fail_status"
  echo "diag_mode=$diag_mode"
  echo "diag_pattern=$diag_pattern"
  echo "diag_ok=$diag_ok"
  echo "diag_chain_pattern=$diag_chain_pattern"
  echo "diag_chain_ok=$diag_chain_ok"
  echo "build_ok_log=$build_ok_log"
  echo "run_ok_log=$run_ok_log"
  echo "build_fail_log=$build_fail_log"
} >"$report"

{
  echo "backend_import_cycle_predeclare_target=$target"
  echo "backend_import_cycle_predeclare_diag_mode=$diag_mode"
  echo "backend_import_cycle_predeclare_diag_ok=$diag_ok"
  echo "backend_import_cycle_predeclare_diag_chain_ok=$diag_chain_ok"
  echo "backend_import_cycle_predeclare_report=$report"
} >"$snapshot"

echo "verify_backend_import_cycle_predeclare ok"
