#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
scripts_dir="src/tooling/cheng_tooling_embedded_scripts"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "help" ]; then
  echo "Usage: ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} verify [--help]"
  echo "Runs default repository verification gates through cheng_tooling subcommands."
  exit 0
fi

run_cmd() {
  name="$1"
  shift || true
  echo "== $name =="
  "$@"
}

run_cmd "verify.cheng_skill_consistency" "$tool" verify_cheng_skill_consistency
run_cmd "verify.tooling_cmdline" sh "$scripts_dir/verify_tooling_cmdline.sh"
run_cmd "verify.backend_stage1_fixed0_envs" sh "$scripts_dir/verify_backend_stage1_fixed0_envs.sh"
run_cmd "verify.backend_string_literal_regression" sh "$scripts_dir/verify_backend_string_literal_regression.sh"
run_cmd "verify.std_strformat" sh "$scripts_dir/verify_std_strformat.sh"
run_cmd "verify.backend_default_output_safety" sh "$scripts_dir/verify_backend_default_output_safety.sh"
run_cmd "verify.stage1_seed_layout" "$tool" verify_stage1_seed_layout
run_cmd "verify.no_legacy_net_multiformats_imports" "$tool" verify_no_legacy_net_multiformats_imports

if [ "${VERIFY_NO_ROOT_BUILD_ARTIFACTS:-1}" = "1" ]; then
  run_cmd "verify.no_root_build_artifacts" "$tool" verify_no_root_build_artifacts
fi
if [ "${BACKEND_CLOSEDLOOP:-0}" = "1" ]; then
  run_cmd "verify.backend_closedloop" "$tool" verify_backend_closedloop
fi
if [ "${BACKEND_PROD_CLOSURE:-1}" = "1" ]; then
  run_cmd "verify.backend_prod_closure" "$tool" backend_prod_closure --no-publish
fi

echo "verify ok"
