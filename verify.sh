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

run_case() {
  local mm="$1"
  local name="$2"
  local strict="${3:-0}"
  local envs=()
  local fixture="tests/cheng/backend/fixtures/return_add.cheng"

  if [[ "$mm" != "" ]]; then
    envs+=(CHENG_MM="$mm")
  fi
  if [[ "$strict" == "1" ]]; then
    envs+=(CHENG_MM_STRICT=1)
  fi

  run_cmd "build.${name}" env "${envs[@]}" src/tooling/chengc.sh "$fixture" --name:"$name"
  run_cmd "run.${name}" env "${envs[@]}" "./$name"
}

run_cmd "verify.stage1_seed_layout" sh src/tooling/verify_stage1_seed_layout.sh
run_case orc test_orc_closedloop 0
if [[ "${CHENG_VERIFY_MM_OFF:-0}" == "1" ]]; then
  run_case off test_orc_closedloop_off 0
else
  echo "== build.test_orc_closedloop_off (skip: set CHENG_VERIFY_MM_OFF=1 to enable) =="
fi
run_case orc test_orc_closedloop_strict 1
run_cmd "build.test_pkg_import_srcroot" env CHENG_PKG_ROOTS="tests/cheng/backend/pkgs_srcroot/cid_bafytest" \
  src/tooling/chengc.sh tests/cheng/backend/fixtures/return_pkg_import_call_srcroot.cheng --name:test_pkg_import_srcroot
run_cmd "run.test_pkg_import_srcroot" ./test_pkg_import_srcroot
run_cmd "var_borrow_diag" ./scripts/verify_var_borrow_diag.sh
if [ "${CHENG_STAGE1_FULLSPEC:-}" = "1" ]; then
  run_cmd "verify.stage1_fullspec" sh src/tooling/verify_stage1_fullspec.sh
fi
if [ "${CHENG_BACKEND_CLOSEDLOOP:-}" = "1" ]; then
  run_cmd "verify.backend_closedloop" sh src/tooling/verify_backend_closedloop.sh
fi
if [ "${CHENG_C_BACKEND_CLOSURE:-}" = "1" ]; then
  closure_args="${CHENG_C_BACKEND_CLOSURE_ARGS:-}"
  run_cmd "verify.c_backend_closure" sh src/tooling/c_backend_prod_closure.sh $closure_args
fi
if [ "${CHENG_BACKEND_PROD_CLOSURE:-}" = "1" ]; then
  run_cmd "verify.backend_prod_closure" sh src/tooling/backend_prod_closure.sh --no-publish
fi

echo "verify ok"
