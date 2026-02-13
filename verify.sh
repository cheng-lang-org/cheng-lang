#!/usr/bin/env bash
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$root"

export CHENG_BACKEND_PROD_GATE_TIMEOUT="${CHENG_BACKEND_PROD_GATE_TIMEOUT:-60}"
export CHENG_BACKEND_PROD_SELFHOST_TIMEOUT="${CHENG_BACKEND_PROD_SELFHOST_TIMEOUT:-60}"
export CHENG_BACKEND_PROD_TIMEOUT_DIAG="${CHENG_BACKEND_PROD_TIMEOUT_DIAG:-1}"
export CHENG_BACKEND_PROD_TIMEOUT_DIAG_SECONDS="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_SECONDS:-5}"
export CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY:-1}"
export CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP="${CHENG_BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP:-12}"
export CHENG_FRONTIER_TIMEOUT="${CHENG_FRONTIER_TIMEOUT:-60}"
export CHENG_FRONTIER_IMPORT_TIMEOUT="${CHENG_FRONTIER_IMPORT_TIMEOUT:-30}"
export CHENG_FRONTIER_TIMEOUT_DIAG_SUMMARY="${CHENG_FRONTIER_TIMEOUT_DIAG_SUMMARY:-1}"
export CHENG_FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP="${CHENG_FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP:-12}"

run_cmd() {
  local name="$1"
  shift
  echo "== ${name} =="
  "$@"
}

resolve_chengc_exe_path() {
  local name="$1"
  case "$name" in
    /*|*/*)
      printf '%s\n' "$name"
      return
      ;;
  esac
  case "${CHENGC_NAME_IN_ROOT:-0}" in
    1|true|TRUE|yes|YES|on|ON)
      printf '%s\n' "$name"
      ;;
    *)
      printf 'artifacts/chengc/%s\n' "$name"
      ;;
  esac
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
  local frontend="${5:-${CHENG_VERIFY_FRONTEND:-mvp}}"
  local envs=()
  local out=""

  if [[ "$mm" != "" ]]; then
    envs+=(CHENG_MM="$mm")
  fi
  if [[ "$strict" == "1" ]]; then
    envs+=(CHENG_MM_STRICT=1)
  fi
  envs+=(CHENG_BACKEND_FRONTEND="$frontend")
  out="$(resolve_chengc_exe_path "$name")"

  run_cmd "build.${name}" env "${envs[@]}" src/tooling/chengc.sh "$fixture" --name:"$name"
  run_cmd "run.${name}" env "${envs[@]}" "$out"
}

resolve_gate_mode() {
  local mode="${1:-}"
  local compat="${2:-}"
  if [[ "$mode" == "" ]]; then
    case "$compat" in
      1|true|TRUE|yes|YES|on|ON) mode="run" ;;
      0|false|FALSE|no|NO|off|OFF|"") mode="build" ;;
      build|run|skip) mode="$compat" ;;
      *) mode="build" ;;
    esac
  fi
  printf '%s\n' "$mode"
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
list_comp_mode="$(resolve_gate_mode "${CHENG_VERIFY_LIST_COMP_MODE:-}" "${CHENG_VERIFY_LIST_COMP:-}")"
case "$list_comp_mode" in
  build)
    run_cmd "build.test_orc_list_comp.obj" env \
      CHENG_BACKEND_FRONTEND="${CHENG_VERIFY_LIST_COMP_BUILD_FRONTEND:-mvp}" \
      src/tooling/chengc.sh tests/cheng/backend/fixtures/return_list_comp.cheng \
      --emit-obj --obj-out:chengcache/verify_list_comp.obj
    ;;
  run)
    run_case orc test_orc_list_comp 0 tests/cheng/backend/fixtures/return_list_comp.cheng "${CHENG_VERIFY_LIST_COMP_RUN_FRONTEND:-stage1}"
    ;;
  skip)
    echo "== build.test_orc_list_comp (skip: CHENG_VERIFY_LIST_COMP_MODE=skip) =="
    ;;
  *)
    echo "[verify] invalid CHENG_VERIFY_LIST_COMP_MODE=$list_comp_mode (expected build|run|skip)" 1>&2
    exit 2
    ;;
esac
if [[ "${CHENG_VERIFY_MM_OFF:-0}" == "1" ]]; then
  run_case off test_orc_closedloop_off 0
else
  echo "== build.test_orc_closedloop_off (skip: set CHENG_VERIFY_MM_OFF=1 to enable) =="
fi
run_case orc test_orc_closedloop_strict 1
pkg_import_mode="$(resolve_gate_mode "${CHENG_VERIFY_PKG_IMPORT_MODE:-}" "${CHENG_VERIFY_PKG_IMPORT:-}")"
case "$pkg_import_mode" in
  build)
    run_cmd "build.test_pkg_import_srcroot.obj" env \
      CHENG_BACKEND_FRONTEND="${CHENG_VERIFY_PKG_IMPORT_BUILD_FRONTEND:-stage1}" \
      CHENG_STAGE1_SKIP_SEM="${CHENG_VERIFY_PKG_IMPORT_BUILD_SKIP_SEM:-1}" \
      CHENG_STAGE1_SKIP_OWNERSHIP="${CHENG_VERIFY_PKG_IMPORT_BUILD_SKIP_OWNERSHIP:-1}" \
      CHENG_GENERIC_MODE="${CHENG_VERIFY_PKG_IMPORT_BUILD_GENERIC_MODE:-dict}" \
      CHENG_GENERIC_SPEC_BUDGET="${CHENG_VERIFY_PKG_IMPORT_BUILD_GENERIC_SPEC_BUDGET:-0}" \
      CHENG_PKG_ROOTS="tests/cheng/backend/pkgs_srcroot/cid_bafytest" \
      src/tooling/chengc.sh tests/cheng/backend/fixtures/return_pkg_import_call_srcroot.cheng \
      --emit-obj --obj-out:chengcache/verify_pkg_import_srcroot.obj
    ;;
  run)
    run_cmd "build.test_pkg_import_srcroot" env CHENG_BACKEND_FRONTEND="${CHENG_VERIFY_PKG_IMPORT_FRONTEND:-stage1}" CHENG_PKG_ROOTS="tests/cheng/backend/pkgs_srcroot/cid_bafytest" \
      src/tooling/chengc.sh tests/cheng/backend/fixtures/return_pkg_import_call_srcroot.cheng --name:test_pkg_import_srcroot
    run_cmd "run.test_pkg_import_srcroot" "$(resolve_chengc_exe_path test_pkg_import_srcroot)"
    ;;
  skip)
    echo "== build.test_pkg_import_srcroot (skip: CHENG_VERIFY_PKG_IMPORT_MODE=skip) =="
    ;;
  *)
    echo "[verify] invalid CHENG_VERIFY_PKG_IMPORT_MODE=$pkg_import_mode (expected build|run|skip)" 1>&2
    exit 2
    ;;
esac
if [ "${CHENG_STAGE1_FULLSPEC:-}" = "1" ]; then
  run_cmd "verify.stage1_fullspec" sh src/tooling/verify_stage1_fullspec.sh
fi
if [ "${CHENG_BACKEND_CLOSEDLOOP:-}" = "1" ]; then
  run_cmd "verify.backend_closedloop" sh src/tooling/verify_backend_closedloop.sh
fi
if [ "${CHENG_BACKEND_PROD_CLOSURE:-}" = "1" ]; then
  run_cmd "verify.backend_prod_closure" sh src/tooling/backend_prod_closure.sh --no-publish
fi
if [ "${CHENG_VERIFY_LIBP2P_FRONTIER:-}" = "1" ]; then
  run_cmd "verify.libp2p_frontier" sh src/tooling/verify_libp2p_frontier.sh
fi

echo "verify ok"
