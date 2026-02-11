#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$root"

run_cmd() {
  local name="$1"
  shift
  echo "== ${name} =="
  "$@"
}

run_skill_consistency_gate() {
  local uname_s="${1:-}"
  case "$uname_s" in
    MINGW*|MSYS*|CYGWIN*)
      run_cmd "verify.cheng_skill_consistency" env \
        CHENG_SKILL_COMPILE="${CHENG_SKILL_COMPILE:-0}" \
        CHENG_SKILL_COMPARE_HOME="${CHENG_SKILL_COMPARE_HOME:-0}" \
        sh src/tooling/verify_cheng_skill_consistency.sh
      ;;
    *)
      run_cmd "verify.cheng_skill_consistency" sh src/tooling/verify_cheng_skill_consistency.sh
      ;;
  esac
}

run_case() {
  local mm="$1"
  local name="$2"
  local strict="${3:-0}"
  local fixture="${4:-tests/cheng/backend/fixtures/return_add.cheng}"
  local envs=()

  if [[ "$mm" != "" ]]; then
    envs+=(CHENG_MM="$mm")
  fi
  if [[ "$strict" == "1" ]]; then
    envs+=(CHENG_MM_STRICT=1)
  fi

  run_cmd "build.${name}" env "${envs[@]}" src/tooling/chengc.sh "$fixture" --name:"$name"
  run_cmd "run.${name}" env "${envs[@]}" "./$name"
}

if [[ "${CHENG_VERIFY_SKILL:-1}" == "1" ]]; then
  run_skill_consistency_gate "$(uname -s)"
else
  echo "== verify.cheng_skill_consistency (skip: set CHENG_VERIFY_SKILL=1 to enable) =="
fi

if [[ "${CHENG_VERIFY_RUNTIME_ABI:-1}" == "1" ]]; then
  run_cmd "verify.backend_runtime_abi" sh src/tooling/verify_backend_runtime_abi.sh
else
  echo "== verify.backend_runtime_abi (skip: set CHENG_VERIFY_RUNTIME_ABI=1 to enable) =="
fi

run_cmd "verify.stage1_seed_layout" sh src/tooling/verify_stage1_seed_layout.sh
run_case orc test_orc_closedloop 0
run_case orc test_orc_list_comp 0 tests/cheng/backend/fixtures/return_list_comp.cheng
if [[ "${CHENG_VERIFY_MM_OFF:-0}" == "1" ]]; then
  run_case off test_orc_closedloop_off 0
else
  echo "== build.test_orc_closedloop_off (skip: set CHENG_VERIFY_MM_OFF=1 to enable) =="
fi
run_case orc test_orc_closedloop_strict 1
run_cmd "build.test_pkg_import_srcroot" env CHENG_PKG_ROOTS="tests/cheng/backend/pkgs_srcroot/cid_bafytest" \
  src/tooling/chengc.sh tests/cheng/backend/fixtures/return_pkg_import_call_srcroot.cheng --name:test_pkg_import_srcroot
run_cmd "run.test_pkg_import_srcroot" ./test_pkg_import_srcroot
if [ "${CHENG_STAGE1_FULLSPEC:-}" = "1" ]; then
  run_cmd "verify.stage1_fullspec" sh src/tooling/verify_stage1_fullspec.sh
fi
if [ "${CHENG_BACKEND_CLOSEDLOOP:-}" = "1" ]; then
  run_cmd "verify.backend_closedloop" sh src/tooling/verify_backend_closedloop.sh
fi
if [ "${CHENG_BACKEND_PROD_CLOSURE:-}" = "1" ]; then
  run_cmd "verify.backend_prod_closure" sh src/tooling/backend_prod_closure.sh --no-publish
fi

echo "verify ok"
