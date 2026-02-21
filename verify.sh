#!/usr/bin/env bash
. "$(CDPATH= cd -- "$(dirname -- "$0")/src/tooling" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$root"

export BACKEND_PROD_GATE_TIMEOUT="${BACKEND_PROD_GATE_TIMEOUT:-60}"
export BACKEND_PROD_SELFHOST_TIMEOUT="${BACKEND_PROD_SELFHOST_TIMEOUT:-120}"
export BACKEND_PROD_TIMEOUT_DIAG="${BACKEND_PROD_TIMEOUT_DIAG:-1}"
export BACKEND_PROD_TIMEOUT_DIAG_SECONDS="${BACKEND_PROD_TIMEOUT_DIAG_SECONDS:-5}"
export BACKEND_PROD_TIMEOUT_DIAG_SUMMARY="${BACKEND_PROD_TIMEOUT_DIAG_SUMMARY:-1}"
export BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP="${BACKEND_PROD_TIMEOUT_DIAG_SUMMARY_TOP:-12}"
export FRONTIER_TIMEOUT="${FRONTIER_TIMEOUT:-60}"
export FRONTIER_IMPORT_TIMEOUT="${FRONTIER_IMPORT_TIMEOUT:-30}"
export FRONTIER_TIMEOUT_DIAG_SUMMARY="${FRONTIER_TIMEOUT_DIAG_SUMMARY:-1}"
export FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP="${FRONTIER_TIMEOUT_DIAG_SUMMARY_TOP:-12}"
export BACKEND_PROD_CLOSURE="${BACKEND_PROD_CLOSURE:-1}"
export BACKEND_RUN_SELFHOST="${BACKEND_RUN_SELFHOST:-0}"

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
  printf 'artifacts/chengc/%s\n' "$name"
}

run_skill_consistency_gate() {
  local uname_s="${1:-}"
  case "$uname_s" in
    MINGW*|MSYS*|CYGWIN*)
      run_cmd "verify.cheng_skill_consistency" env \
        SKILL_COMPILE="${SKILL_COMPILE:-0}" \
        SKILL_COMPARE_HOME="${SKILL_COMPARE_HOME:-0}" \
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
  local frontend="${5:-${VERIFY_FRONTEND:-stage1}}"
  local frontend_name="${6:-VERIFY_FRONTEND}"
  local envs=()
  local out=""

  if [[ "$mm" != "" ]]; then
    envs+=(MM="$mm")
  fi
  if [[ "$strict" == "1" ]]; then
    envs+=(MM_STRICT=1)
  fi
  envs+=(ABI=v2_noptr)
  if [[ -n "${VERIFY_CHENGC_DRIVER:-}" ]]; then
    envs+=(BACKEND_DRIVER="${VERIFY_CHENGC_DRIVER}")
  elif [[ -x "artifacts/backend_seed/cheng.stage2" ]]; then
    envs+=(BACKEND_DRIVER="artifacts/backend_seed/cheng.stage2")
  fi
  envs+=(STAGE1_STD_NO_POINTERS=0)
  envs+=(STAGE1_STD_NO_POINTERS_STRICT=0)
  envs+=(STAGE1_NO_POINTERS_NON_C_ABI=0)
  envs+=(STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0)
  validate_stage1_frontend "$frontend_name" "$frontend"
  envs+=(BACKEND_FRONTEND="$frontend")
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

validate_stage1_frontend() {
  local name="$1"
  local value="$2"
  if [[ "$value" != "stage1" ]]; then
    echo "[verify] invalid ${name}=${value} (expected stage1)" 1>&2
    exit 2
  fi
}

setup_stable_drivers_if_needed() {
  if [[ "${VERIFY_USE_STABLE_DRIVERS:-1}" != "1" ]]; then
    return
  fi
  if [[ -z "${BACKEND_DRIVER:-}" ]] && [[ -x "artifacts/backend_driver/cheng.fixed3" ]]; then
    export BACKEND_DRIVER="artifacts/backend_driver/cheng.fixed3"
  fi
  if [[ -x "artifacts/backend_seed/cheng.stage2" ]]; then
    export BACKEND_OPT_DRIVER="${BACKEND_OPT_DRIVER:-artifacts/backend_seed/cheng.stage2}"
    export BACKEND_LINKERLESS_DRIVER="${BACKEND_LINKERLESS_DRIVER:-artifacts/backend_seed/cheng.stage2}"
    export BACKEND_MULTI_DRIVER="${BACKEND_MULTI_DRIVER:-artifacts/backend_seed/cheng.stage2}"
    export BACKEND_STAGE1_SMOKE_DRIVER="${BACKEND_STAGE1_SMOKE_DRIVER:-artifacts/backend_seed/cheng.stage2}"
    export BACKEND_MM_DRIVER="${BACKEND_MM_DRIVER:-artifacts/backend_seed/cheng.stage2}"
  fi
}

if [[ "${VERIFY_SKILL:-1}" == "1" ]]; then
  run_skill_consistency_gate "$(uname -s)"
else
  echo "== verify.cheng_skill_consistency (skip: set VERIFY_SKILL=1 to enable) =="
fi

if [[ "${VERIFY_RUNTIME_ABI:-1}" == "1" ]]; then
  run_cmd "verify.backend_runtime_abi" sh src/tooling/verify_backend_runtime_abi.sh
else
  echo "== verify.backend_runtime_abi (skip: set VERIFY_RUNTIME_ABI=1 to enable) =="
fi

run_cmd "verify.stage1_seed_layout" sh src/tooling/verify_stage1_seed_layout.sh
run_cmd "verify.no_legacy_net_multiformats_imports" sh src/tooling/verify_no_legacy_net_multiformats_imports.sh
if [[ "${VERIFY_NO_ROOT_BUILD_ARTIFACTS:-1}" == "1" ]]; then
  run_cmd "verify.no_root_build_artifacts" sh src/tooling/verify_no_root_build_artifacts.sh
else
  echo "== verify.no_root_build_artifacts (skip: set VERIFY_NO_ROOT_BUILD_ARTIFACTS=1 to enable) =="
fi
setup_stable_drivers_if_needed
run_case orc test_orc_closedloop 0
list_comp_mode="$(resolve_gate_mode "${VERIFY_LIST_COMP_MODE:-}" "${VERIFY_LIST_COMP:-}")"
case "$list_comp_mode" in
  build)
    list_comp_build_frontend="${VERIFY_LIST_COMP_BUILD_FRONTEND:-stage1}"
    validate_stage1_frontend "VERIFY_LIST_COMP_BUILD_FRONTEND" "$list_comp_build_frontend"
    run_cmd "build.test_orc_list_comp.obj" env \
      ABI=v2_noptr \
      BACKEND_DRIVER="${VERIFY_CHENGC_DRIVER:-artifacts/backend_seed/cheng.stage2}" \
      STAGE1_STD_NO_POINTERS=0 \
      STAGE1_STD_NO_POINTERS_STRICT=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      BACKEND_FRONTEND="$list_comp_build_frontend" \
      src/tooling/chengc.sh tests/cheng/backend/fixtures/return_list_comp.cheng \
      --emit-obj --obj-out:chengcache/verify_list_comp.obj
    ;;
  run)
    run_case orc test_orc_list_comp 0 tests/cheng/backend/fixtures/return_list_comp.cheng "${VERIFY_LIST_COMP_RUN_FRONTEND:-stage1}" "VERIFY_LIST_COMP_RUN_FRONTEND"
    ;;
  skip)
    echo "== build.test_orc_list_comp (skip: VERIFY_LIST_COMP_MODE=skip) =="
    ;;
  *)
    echo "[verify] invalid VERIFY_LIST_COMP_MODE=$list_comp_mode (expected build|run|skip)" 1>&2
    exit 2
    ;;
esac
if [[ "${VERIFY_MM_OFF:-0}" == "1" ]]; then
  run_case off test_orc_closedloop_off 0
else
  echo "== build.test_orc_closedloop_off (skip: set VERIFY_MM_OFF=1 to enable) =="
fi
run_case orc test_orc_closedloop_strict 1
pkg_import_mode="$(resolve_gate_mode "${VERIFY_PKG_IMPORT_MODE:-}" "${VERIFY_PKG_IMPORT:-}")"
case "$pkg_import_mode" in
  build)
    pkg_import_build_frontend="${VERIFY_PKG_IMPORT_BUILD_FRONTEND:-stage1}"
    validate_stage1_frontend "VERIFY_PKG_IMPORT_BUILD_FRONTEND" "$pkg_import_build_frontend"
    run_cmd "build.test_pkg_import_srcroot.obj" env \
      ABI=v2_noptr \
      BACKEND_DRIVER="${VERIFY_CHENGC_DRIVER:-artifacts/backend_seed/cheng.stage2}" \
      STAGE1_STD_NO_POINTERS=0 \
      STAGE1_STD_NO_POINTERS_STRICT=0 \
      STAGE1_NO_POINTERS_NON_C_ABI=0 \
      STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
      BACKEND_FRONTEND="$pkg_import_build_frontend" \
      STAGE1_SKIP_SEM="${VERIFY_PKG_IMPORT_BUILD_SKIP_SEM:-1}" \
      STAGE1_SKIP_OWNERSHIP="${VERIFY_PKG_IMPORT_BUILD_SKIP_OWNERSHIP:-1}" \
      GENERIC_MODE="${VERIFY_PKG_IMPORT_BUILD_GENERIC_MODE:-dict}" \
      GENERIC_SPEC_BUDGET="${VERIFY_PKG_IMPORT_BUILD_GENERIC_SPEC_BUDGET:-0}" \
      PKG_ROOTS="tests/cheng/backend/pkgs_srcroot/cid_bafytest" \
      src/tooling/chengc.sh tests/cheng/backend/fixtures/return_pkg_import_call_srcroot.cheng \
      --emit-obj --obj-out:chengcache/verify_pkg_import_srcroot.obj
    ;;
  run)
    pkg_import_run_frontend="${VERIFY_PKG_IMPORT_FRONTEND:-stage1}"
    validate_stage1_frontend "VERIFY_PKG_IMPORT_FRONTEND" "$pkg_import_run_frontend"
    run_cmd "build.test_pkg_import_srcroot" env ABI=v2_noptr BACKEND_DRIVER="${VERIFY_CHENGC_DRIVER:-artifacts/backend_seed/cheng.stage2}" STAGE1_STD_NO_POINTERS=0 STAGE1_STD_NO_POINTERS_STRICT=0 STAGE1_NO_POINTERS_NON_C_ABI=0 STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 BACKEND_FRONTEND="$pkg_import_run_frontend" PKG_ROOTS="tests/cheng/backend/pkgs_srcroot/cid_bafytest" \
      src/tooling/chengc.sh tests/cheng/backend/fixtures/return_pkg_import_call_srcroot.cheng --name:test_pkg_import_srcroot
    run_cmd "run.test_pkg_import_srcroot" "$(resolve_chengc_exe_path test_pkg_import_srcroot)"
    ;;
  skip)
    echo "== build.test_pkg_import_srcroot (skip: VERIFY_PKG_IMPORT_MODE=skip) =="
    ;;
  *)
    echo "[verify] invalid VERIFY_PKG_IMPORT_MODE=$pkg_import_mode (expected build|run|skip)" 1>&2
    exit 2
    ;;
esac
if [ "${STAGE1_FULLSPEC:-}" = "1" ]; then
  run_cmd "verify.stage1_fullspec" sh src/tooling/verify_stage1_fullspec.sh
fi
if [ "${BACKEND_CLOSEDLOOP:-}" = "1" ]; then
  run_cmd "verify.backend_closedloop" sh src/tooling/verify_backend_closedloop.sh
fi
if [ "${BACKEND_PROD_CLOSURE:-}" = "1" ]; then
  run_cmd "verify.backend_prod_closure" sh src/tooling/backend_prod_closure.sh --no-publish
fi
if [ "${VERIFY_LIBP2P_FRONTIER:-}" = "1" ]; then
  run_cmd "verify.libp2p_frontier" sh src/tooling/verify_libp2p_frontier.sh
fi

echo "verify ok"
