#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
repo_tool="${TOOLING_REPO_SH:-$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh}"
real_tool="${TOOLING_REAL_BIN:-$root/artifacts/tooling_cmd/cheng_tooling.real}"
real_tool_src="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling_real.sh"
real_primary_src="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling_real_bin.sh"
real_primary_bin="$root/artifacts/tooling_cmd/cheng_tooling.real.bin"
core_bin="$root/artifacts/tooling_bundle/core/cheng_tooling_global"
artifacts_dir="$root/artifacts"
final_out_dir="$artifacts_dir/tooling_cmdline"
mkdir -p "$artifacts_dir"
work_dir="$(mktemp -d "$artifacts_dir/tooling_cmdline.work.XXXXXX")"
publish_dir=""
cleanup() {
  keep_work="${TOOLING_CMDLINE_KEEP_WORK:-0}"
  if [ "$publish_dir" != "" ] && [ -e "$publish_dir" ]; then
    rm -rf "$publish_dir" 2>/dev/null || true
  fi
  if [ "$keep_work" != "1" ] && [ "$work_dir" != "" ] && [ -e "$work_dir" ]; then
    rm -rf "$work_dir" 2>/dev/null || true
  fi
  return 0
}
trap cleanup EXIT INT TERM
out_dir="$work_dir"

list_out="$out_dir/tooling_cmdline.list.txt"
global_list_out="$out_dir/tooling_cmdline.global.list.txt"
help_out="$out_dir/tooling_cmdline.backend_prod_closure.help.txt"
repo_chengc_help_out="$out_dir/tooling_cmdline.repo_chengc.help.txt"
tool_chengc_help_out="$out_dir/tooling_cmdline.tool_chengc.help.txt"
help_out_multicall="$out_dir/tooling_cmdline.backend_prod_closure.multicall.help.txt"
help_out_multicall_chengc="$out_dir/tooling_cmdline.chengc.multicall.help.txt"
help_out_multicall_new_expr="$out_dir/tooling_cmdline.verify_new_expr_surface.multicall.help.txt"
help_out_multicall_currentsrc_proof="$out_dir/tooling_cmdline.verify_backend_selfhost_currentsrc_proof.multicall.help.txt"
help_out_multicall_mir_borrow="$out_dir/tooling_cmdline.verify_backend_mir_borrow.multicall.help.txt"
help_out_multicall_string_abi="$out_dir/tooling_cmdline.verify_backend_string_abi_contract.multicall.help.txt"
help_out_multicall_dot_lowering="$out_dir/tooling_cmdline.verify_backend_dot_lowering_contract.multicall.help.txt"
repo_verify_help_out="$out_dir/tooling_cmdline.repo_verify.help.txt"
repo_backend_prod_publish_alias_help_out="$out_dir/tooling_cmdline.repo_backend_prod_publish.alias.help.txt"
repo_selfhost_100ms_alias_help_out="$out_dir/tooling_cmdline.repo_selfhost_100ms_host.alias.help.txt"
repo_fixed0_out="$out_dir/tooling_cmdline.repo_fixed0.txt"
repo_strlit_out="$out_dir/tooling_cmdline.repo_string_literal.txt"
repo_strformat_out="$out_dir/tooling_cmdline.repo_std_strformat.txt"
repo_output_safety_out="$out_dir/tooling_cmdline.repo_backend_default_output_safety.txt"
repo_new_expr_out="$out_dir/tooling_cmdline.repo_verify_new_expr_surface.txt"
repo_currentsrc_proof_out="$out_dir/tooling_cmdline.repo_backend_selfhost_currentsrc_proof.txt"
repo_mir_borrow_out="$out_dir/tooling_cmdline.repo_backend_mir_borrow.txt"
repo_string_abi_out="$out_dir/tooling_cmdline.repo_backend_string_abi_contract.txt"
repo_dot_lowering_out="$out_dir/tooling_cmdline.repo_backend_dot_lowering_contract.txt"
tool_verify_help_out="$out_dir/tooling_cmdline.tool_verify.help.txt"
tool_backend_prod_publish_alias_help_out="$out_dir/tooling_cmdline.tool_backend_prod_publish.alias.help.txt"
tool_selfhost_100ms_alias_help_out="$out_dir/tooling_cmdline.tool_selfhost_100ms_host.alias.help.txt"
tool_fixed0_out="$out_dir/tooling_cmdline.tool_fixed0.txt"
tool_strlit_out="$out_dir/tooling_cmdline.tool_string_literal.txt"
tool_strformat_out="$out_dir/tooling_cmdline.tool_std_strformat.txt"
tool_output_safety_out="$out_dir/tooling_cmdline.tool_backend_default_output_safety.txt"
tool_new_expr_out="$out_dir/tooling_cmdline.tool_verify_new_expr_surface.txt"
tool_currentsrc_proof_out="$out_dir/tooling_cmdline.tool_backend_selfhost_currentsrc_proof.txt"
tool_mir_borrow_out="$out_dir/tooling_cmdline.tool_backend_mir_borrow.txt"
tool_string_abi_out="$out_dir/tooling_cmdline.tool_backend_string_abi_contract.txt"
tool_dot_lowering_out="$out_dir/tooling_cmdline.tool_backend_dot_lowering_contract.txt"
real_list_out="$out_dir/tooling_cmdline.real.list.txt"
real_primary_list_out="$out_dir/tooling_cmdline.real_primary.list.txt"
real_verify_help_out="$out_dir/tooling_cmdline.real_verify.help.txt"
real_chengc_help_out="$out_dir/tooling_cmdline.real_chengc.help.txt"
real_fixed0_out="$out_dir/tooling_cmdline.real_fixed0.txt"
real_strlit_out="$out_dir/tooling_cmdline.real_string_literal.txt"
real_strformat_out="$out_dir/tooling_cmdline.real_std_strformat.txt"
real_output_safety_out="$out_dir/tooling_cmdline.real_backend_default_output_safety.txt"
real_new_expr_out="$out_dir/tooling_cmdline.real_verify_new_expr_surface.txt"
real_currentsrc_proof_out="$out_dir/tooling_cmdline.real_backend_selfhost_currentsrc_proof.txt"
real_mir_borrow_out="$out_dir/tooling_cmdline.real_backend_mir_borrow.txt"
real_core_backend_prod_help_out="$out_dir/tooling_cmdline.real_core_backend_prod_closure.help.txt"
real_primary_verify_help_out="$out_dir/tooling_cmdline.real_primary_verify.help.txt"
real_primary_chengc_help_out="$out_dir/tooling_cmdline.real_primary_chengc.help.txt"
real_primary_fixed0_out="$out_dir/tooling_cmdline.real_primary_fixed0.txt"
real_primary_strlit_out="$out_dir/tooling_cmdline.real_primary_string_literal.txt"
real_primary_strformat_out="$out_dir/tooling_cmdline.real_primary_std_strformat.txt"
real_primary_output_safety_out="$out_dir/tooling_cmdline.real_primary_backend_default_output_safety.txt"
real_primary_new_expr_out="$out_dir/tooling_cmdline.real_primary_verify_new_expr_surface.txt"
real_primary_currentsrc_proof_out="$out_dir/tooling_cmdline.real_primary_backend_selfhost_currentsrc_proof.txt"
real_primary_mir_borrow_out="$out_dir/tooling_cmdline.real_primary_backend_mir_borrow.txt"
global_bin="$out_dir/cheng_tooling"
install_dir="$out_dir/bin"
install_manifest="$out_dir/tooling_cmdline.install_manifest.tsv"
install_stdout="$out_dir/tooling_cmdline.install.stdout.txt"
bundle_dir="$root/artifacts/tooling_bundle/full"
bundle_manifest="$bundle_dir/manifest.tsv"
bundle_new_expr_bin="$bundle_dir/bin/verify_new_expr_surface"
bundle_new_expr_help_out="$out_dir/tooling_cmdline.bundle.verify_new_expr_surface.help.txt"
bundle_currentsrc_proof_bin="$bundle_dir/bin/verify_backend_selfhost_currentsrc_proof"
bundle_currentsrc_proof_help_out="$out_dir/tooling_cmdline.bundle.verify_backend_selfhost_currentsrc_proof.help.txt"
bundle_mir_borrow_bin="$bundle_dir/bin/verify_backend_mir_borrow"
bundle_mir_borrow_help_out="$out_dir/tooling_cmdline.bundle.verify_backend_mir_borrow.help.txt"
bundle_string_abi_bin="$bundle_dir/bin/verify_backend_string_abi_contract"
bundle_dot_lowering_bin="$bundle_dir/bin/verify_backend_dot_lowering_contract"
bundle_string_abi_help_out="$out_dir/tooling_cmdline.bundle.verify_backend_string_abi_contract.help.txt"
bundle_dot_lowering_help_out="$out_dir/tooling_cmdline.bundle.verify_backend_dot_lowering_contract.help.txt"
report="$out_dir/tooling_cmdline.report.txt"
currentsrc_proof_out_dir="$out_dir/tooling_cmdline.currentsrc_proof"
currentsrc_proof_session="tooling.cmdline.currentsrc.proof"
published_currentsrc_proof_dir="$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof"

run_gate_sanitized() {
  env \
    BACKEND_LINKER=system \
    BACKEND_MM_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_DIRECT_EXE=0 \
    BACKEND_LINKERLESS_INMEM=0 \
    BACKEND_FAST_FALLBACK_ALLOW=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    "$@"
}

run_currentsrc_proof_gate() {
  runner="$1"
  log="$2"
  reuse="$3"
  shift 3
  env \
    SELFHOST_CURRENTSRC_PROOF_SESSION="$currentsrc_proof_session" \
    SELFHOST_CURRENTSRC_PROOF_OUT_DIR="$currentsrc_proof_out_dir" \
    SELFHOST_CURRENTSRC_PROOF_REUSE="$reuse" \
    SELFHOST_CURRENTSRC_PROOF_SKIP_DIRECT_SMOKE=1 \
    "$runner" "$@" verify_backend_selfhost_currentsrc_proof >"$log" 2>&1
}

run_mir_borrow_gate() {
  runner="$1"
  log="$2"
  shift 2
  run_gate_sanitized "$runner" "$@" verify_backend_mir_borrow >"$log" 2>&1
}

seed_currentsrc_proof_out_dir() {
  if [ ! -d "$published_currentsrc_proof_dir" ]; then
    echo "[verify_tooling_cmdline] missing published current-source proof dir: $published_currentsrc_proof_dir" 1>&2
    exit 1
  fi
  rm -rf "$currentsrc_proof_out_dir"
  mkdir -p "$currentsrc_proof_out_dir"
  cp -R "$published_currentsrc_proof_dir"/. "$currentsrc_proof_out_dir"/
}

run_contract_gate() {
  runner="$1"
  gate_id="$2"
  log="$3"
  shift 3
  run_gate_sanitized "$runner" "$@" "$gate_id" >"$log" 2>&1
}

show_log_excerpt() {
  log_path="$1"
  max_lines="${2:-160}"
  if [ -f "$log_path" ]; then
    sed -n "1,${max_lines}p" "$log_path" 1>&2 || true
  else
    echo "[verify_tooling_cmdline] missing log: $log_path" 1>&2
  fi
}

logged_rc=0
run_logged() {
  log_path="$1"
  shift
  set +e
  "$@" >"$log_path" 2>&1
  logged_rc=$?
  set -e
}

run_logged_expect() {
  log_path="$1"
  cmd_fail_msg="$2"
  pattern="$3"
  pattern_fail_msg="$4"
  max_lines="${5:-160}"
  shift 5
  run_logged "$log_path" "$@"
  if [ "$logged_rc" -ne 0 ]; then
    echo "[verify_tooling_cmdline] $cmd_fail_msg (rc=$logged_rc)" 1>&2
    show_log_excerpt "$log_path" "$max_lines"
    exit 1
  fi
  if [ "$pattern" != "" ] && ! rg -q "$pattern" "$log_path"; then
    echo "[verify_tooling_cmdline] $pattern_fail_msg" 1>&2
    show_log_excerpt "$log_path" "$max_lines"
    exit 1
  fi
}

if [ ! -f "$repo_tool" ]; then
  echo "[verify_tooling_cmdline] missing repo wrapper: $repo_tool" 1>&2
  exit 1
fi
if [ ! -f "$real_tool_src" ]; then
  echo "[verify_tooling_cmdline] missing real launcher template: $real_tool_src" 1>&2
  exit 1
fi
if [ ! -f "$real_primary_src" ]; then
  echo "[verify_tooling_cmdline] missing real primary launcher template: $real_primary_src" 1>&2
  exit 1
fi
if [ ! -x "$real_tool" ]; then
  echo "[verify_tooling_cmdline] missing real tool launcher: $real_tool" 1>&2
  exit 1
fi
if ! cmp -s "$real_tool_src" "$real_tool"; then
  echo "[verify_tooling_cmdline] real launcher drift: $real_tool" 1>&2
  echo "  template: $real_tool_src" 1>&2
  exit 1
fi
if [ ! -x "$core_bin" ]; then
  echo "[verify_tooling_cmdline] missing core tooling binary: $core_bin" 1>&2
  exit 1
fi

run_logged_expect "$repo_verify_help_out" \
  "repo verify --help failed" \
  'Usage:' \
  "repo verify help text missing" \
  120 \
  sh "$repo_tool" verify --help

run_logged_expect "$repo_backend_prod_publish_alias_help_out" \
  "repo backend-prod-publish --help failed" \
  'Usage:|backend-prod-publish options:' \
  "repo backend-prod-publish alias help text missing" \
  120 \
  sh "$repo_tool" backend-prod-publish --help

run_logged_expect "$repo_selfhost_100ms_alias_help_out" \
  "repo selfhost-100ms-host --help failed" \
  'Usage:' \
  "repo selfhost-100ms-host alias help text missing" \
  120 \
  sh "$repo_tool" selfhost-100ms-host --help

run_logged_expect "$repo_fixed0_out" \
  "repo fixed0 gate command failed" \
  'verify_backend_stage1_fixed0_envs ok' \
  "repo fixed0 gate failed" \
  120 \
  run_gate_sanitized sh "$repo_tool" verify_backend_stage1_fixed0_envs

run_logged_expect "$repo_strlit_out" \
  "repo string literal gate command failed" \
  'verify_backend_string_literal_regression ok' \
  "repo string literal gate failed" \
  160 \
  env \
    BACKEND_STRING_LITERAL_TIMEOUT="${BACKEND_STRING_LITERAL_TIMEOUT:-60}" \
    BACKEND_LINKER=system \
    BACKEND_MM_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_DIRECT_EXE=0 \
    BACKEND_LINKERLESS_INMEM=0 \
    BACKEND_FAST_FALLBACK_ALLOW=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    sh "$repo_tool" verify_backend_string_literal_regression

run_logged_expect "$repo_strformat_out" \
  "repo std strformat gate command failed" \
  'verify_std_strformat ok' \
  "repo std strformat gate failed" \
  160 \
  run_gate_sanitized sh "$repo_tool" verify_std_strformat

run_logged_expect "$repo_output_safety_out" \
  "repo backend default output safety gate command failed" \
  'verify_backend_default_output_safety ok' \
  "repo backend default output safety gate failed" \
  160 \
  run_gate_sanitized sh "$repo_tool" verify_backend_default_output_safety

run_logged_expect "$repo_new_expr_out" \
  "repo new-expr surface gate command failed" \
  'verify_new_expr_surface ok' \
  "repo new-expr surface gate failed" \
  160 \
  run_gate_sanitized sh "$repo_tool" verify_new_expr_surface

seed_currentsrc_proof_out_dir
run_logged_expect "$repo_currentsrc_proof_out" \
  "repo current-source proof gate command failed" \
  'verify_backend_selfhost_currentsrc_proof ok' \
  "repo current-source proof gate failed" \
  200 \
  run_currentsrc_proof_gate sh "$repo_currentsrc_proof_out" 1 "$repo_tool"

run_logged_expect "$repo_mir_borrow_out" \
  "repo mir_borrow gate command failed" \
  'verify_backend_mir_borrow ok' \
  "repo mir_borrow gate failed" \
  200 \
  run_mir_borrow_gate sh "$repo_mir_borrow_out" "$repo_tool"

run_logged_expect "$repo_string_abi_out" \
  "repo string ABI contract gate command failed" \
  'verify_backend_string_abi_contract ok' \
  "repo string ABI contract gate failed" \
  200 \
  run_contract_gate sh verify_backend_string_abi_contract "$repo_string_abi_out" "$repo_tool"

run_logged_expect "$repo_dot_lowering_out" \
  "repo dot lowering contract gate command failed" \
  'verify_backend_dot_lowering_contract ok' \
  "repo dot lowering contract gate failed" \
  200 \
  run_contract_gate sh verify_backend_dot_lowering_contract "$repo_dot_lowering_out" "$repo_tool"

run_logged_expect "$repo_chengc_help_out" \
  "repo chengc --help failed" \
  'Usage:' \
  "repo chengc help text missing" \
  120 \
  sh "$repo_tool" chengc --help

run_logged_expect "$list_out" \
  "tool list command failed" \
  "" \
  "" \
  160 \
  env TOOLING_FORCE_BUILD=1 TOOLING_LINKER=system "$tool" list
for id in backend_prod_closure verify_backend_closedloop chengc verify_tooling_cmdline verify_new_expr_surface verify_backend_selfhost_currentsrc_proof verify_backend_mir_borrow verify_backend_string_abi_contract verify_backend_dot_lowering_contract verify_backend_macho_cstring_overlap verify_std_string_json; do
  if ! rg -qx "$id" "$list_out"; then
    echo "[verify_tooling_cmdline] missing id in list output: $id" 1>&2
    exit 1
  fi
done

run_logged_expect "$help_out" \
  "tool backend_prod_closure --help failed" \
  'Usage:' \
  "backend_prod_closure help text missing" \
  120 \
  env TOOLING_LINKER=system "$tool" backend_prod_closure --help

global_bin="${TOOLING_GLOBAL_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
if [ ! -x "$global_bin" ]; then
  global_bin="$tool"
fi
if [ ! -x "$global_bin" ]; then
  echo "[verify_tooling_cmdline] missing runnable global tool binary: $global_bin" 1>&2
  exit 1
fi
run_logged_expect "$global_list_out" \
  "global list command failed" \
  "" \
  "" \
  160 \
  "$global_bin" list

run_logged_expect "$tool_verify_help_out" \
  "global verify --help failed" \
  'Usage:' \
  "global verify help text missing" \
  120 \
  "$global_bin" verify --help

run_logged_expect "$tool_backend_prod_publish_alias_help_out" \
  "global backend-prod-publish --help failed" \
  'Usage:|backend-prod-publish options:' \
  "global backend-prod-publish alias help text missing" \
  120 \
  "$global_bin" backend-prod-publish --help

run_logged_expect "$tool_selfhost_100ms_alias_help_out" \
  "global selfhost-100ms-host --help failed" \
  'Usage:' \
  "global selfhost-100ms-host alias help text missing" \
  120 \
  "$global_bin" selfhost-100ms-host --help

run_logged_expect "$tool_chengc_help_out" \
  "global chengc --help failed" \
  'Usage:' \
  "global chengc help text missing" \
  120 \
  "$global_bin" chengc --help

run_logged_expect "$tool_fixed0_out" \
  "global fixed0 gate command failed" \
  'verify_backend_stage1_fixed0_envs ok' \
  "global fixed0 gate failed" \
  120 \
  run_gate_sanitized "$global_bin" verify_backend_stage1_fixed0_envs

run_logged_expect "$tool_strlit_out" \
  "global string literal gate command failed" \
  'verify_backend_string_literal_regression ok' \
  "global string literal gate failed" \
  160 \
  env \
    BACKEND_STRING_LITERAL_TIMEOUT="${BACKEND_STRING_LITERAL_TIMEOUT:-60}" \
    BACKEND_LINKER=system \
    BACKEND_MM_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_DIRECT_EXE=0 \
    BACKEND_LINKERLESS_INMEM=0 \
    BACKEND_FAST_FALLBACK_ALLOW=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    "$global_bin" verify_backend_string_literal_regression

run_logged_expect "$tool_strformat_out" \
  "global std strformat gate command failed" \
  'verify_std_strformat ok' \
  "global std strformat gate failed" \
  160 \
  run_gate_sanitized "$global_bin" verify_std_strformat

run_logged_expect "$tool_output_safety_out" \
  "global backend default output safety gate command failed" \
  'verify_backend_default_output_safety ok' \
  "global backend default output safety gate failed" \
  160 \
  run_gate_sanitized "$global_bin" verify_backend_default_output_safety

run_logged_expect "$tool_new_expr_out" \
  "global new-expr surface gate command failed" \
  'verify_new_expr_surface ok' \
  "global new-expr surface gate failed" \
  160 \
  run_gate_sanitized "$global_bin" verify_new_expr_surface

run_logged_expect "$tool_currentsrc_proof_out" \
  "global current-source proof gate command failed" \
  'verify_backend_selfhost_currentsrc_proof ok' \
  "global current-source proof gate failed" \
  200 \
  run_currentsrc_proof_gate "$global_bin" "$tool_currentsrc_proof_out" 1

run_logged_expect "$tool_mir_borrow_out" \
  "global mir_borrow gate command failed" \
  'verify_backend_mir_borrow ok' \
  "global mir_borrow gate failed" \
  200 \
  run_mir_borrow_gate "$global_bin" "$tool_mir_borrow_out"

run_logged_expect "$tool_string_abi_out" \
  "global string ABI contract gate command failed" \
  'verify_backend_string_abi_contract ok' \
  "global string ABI contract gate failed" \
  200 \
  run_contract_gate "$global_bin" verify_backend_string_abi_contract "$tool_string_abi_out"

run_logged_expect "$tool_dot_lowering_out" \
  "global dot lowering contract gate command failed" \
  'verify_backend_dot_lowering_contract ok' \
  "global dot lowering contract gate failed" \
  200 \
  run_contract_gate "$global_bin" verify_backend_dot_lowering_contract "$tool_dot_lowering_out"

run_logged_expect "$real_list_out" \
  "real launcher list command failed" \
  "" \
  "" \
  160 \
  "$real_tool" list
for id in chengc backend_prod_closure verify verify_new_expr_surface verify_backend_selfhost_currentsrc_proof verify_backend_mir_borrow verify_backend_string_abi_contract verify_backend_dot_lowering_contract verify_backend_macho_cstring_overlap verify_std_string_json; do
  if ! rg -qx "$id" "$real_list_out"; then
    echo "[verify_tooling_cmdline] real launcher missing id in list output: $id" 1>&2
    exit 1
  fi
done
if [ ! -x "$real_primary_bin" ]; then
  echo "[verify_tooling_cmdline] missing real primary binary: $real_primary_bin" 1>&2
  exit 1
fi
if ! cmp -s "$real_primary_src" "$real_primary_bin"; then
  echo "[verify_tooling_cmdline] real primary binary drift: $real_primary_bin" 1>&2
  echo "  template: $real_primary_src" 1>&2
  exit 1
fi

run_logged_expect "$real_primary_list_out" \
  "real primary binary list command failed" \
  "" \
  "" \
  160 \
  "$real_primary_bin" list
for id in chengc backend_prod_closure verify verify_new_expr_surface verify_backend_selfhost_currentsrc_proof verify_backend_mir_borrow verify_backend_string_abi_contract verify_backend_dot_lowering_contract verify_backend_macho_cstring_overlap verify_std_string_json; do
  if ! rg -qx "$id" "$real_primary_list_out"; then
    echo "[verify_tooling_cmdline] real primary binary missing id in list output: $id" 1>&2
    exit 1
  fi
done

run_logged_expect "$real_primary_verify_help_out" \
  "real primary binary verify --help failed" \
  'Usage:' \
  "real primary binary verify help text missing" \
  120 \
  "$real_primary_bin" verify --help

run_logged_expect "$real_primary_chengc_help_out" \
  "real primary binary chengc --help failed" \
  'Usage:|compile options:' \
  "real primary binary chengc help text missing" \
  120 \
  "$real_primary_bin" chengc --help

run_logged_expect "$real_primary_fixed0_out" \
  "real primary binary fixed0 gate command failed" \
  'verify_backend_stage1_fixed0_envs ok' \
  "real primary binary fixed0 gate failed" \
  120 \
  run_gate_sanitized "$real_primary_bin" verify_backend_stage1_fixed0_envs

run_logged_expect "$real_primary_strlit_out" \
  "real primary binary string literal gate command failed" \
  'verify_backend_string_literal_regression ok' \
  "real primary binary string literal gate failed" \
  160 \
  env \
    BACKEND_STRING_LITERAL_TIMEOUT="${BACKEND_STRING_LITERAL_TIMEOUT:-60}" \
    BACKEND_LINKER=system \
    BACKEND_MM_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_DIRECT_EXE=0 \
    BACKEND_LINKERLESS_INMEM=0 \
    BACKEND_FAST_FALLBACK_ALLOW=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    "$real_primary_bin" verify_backend_string_literal_regression

run_logged_expect "$real_primary_strformat_out" \
  "real primary binary std strformat gate command failed" \
  'verify_std_strformat ok' \
  "real primary binary std strformat gate failed" \
  160 \
  run_gate_sanitized "$real_primary_bin" verify_std_strformat

run_logged_expect "$real_primary_output_safety_out" \
  "real primary binary backend default output safety gate command failed" \
  'verify_backend_default_output_safety ok' \
  "real primary binary backend default output safety gate failed" \
  160 \
  run_gate_sanitized "$real_primary_bin" verify_backend_default_output_safety

run_logged_expect "$real_primary_new_expr_out" \
  "real primary binary new-expr surface gate command failed" \
  'verify_new_expr_surface ok' \
  "real primary binary new-expr surface gate failed" \
  160 \
  run_gate_sanitized "$real_primary_bin" verify_new_expr_surface

run_logged_expect "$real_primary_currentsrc_proof_out" \
  "real primary binary current-source proof gate command failed" \
  'verify_backend_selfhost_currentsrc_proof ok' \
  "real primary binary current-source proof gate failed" \
  200 \
  run_currentsrc_proof_gate "$real_primary_bin" "$real_primary_currentsrc_proof_out" 1

run_logged_expect "$real_primary_mir_borrow_out" \
  "real primary binary mir_borrow gate command failed" \
  'verify_backend_mir_borrow ok' \
  "real primary binary mir_borrow gate failed" \
  200 \
  run_mir_borrow_gate "$real_primary_bin" "$real_primary_mir_borrow_out"

run_logged_expect "$real_verify_help_out" \
  "real launcher verify --help failed" \
  'Usage:' \
  "real launcher verify help text missing" \
  120 \
  "$real_tool" verify --help

run_logged_expect "$real_chengc_help_out" \
  "real launcher chengc --help failed" \
  'Usage:|compile options:' \
  "real launcher chengc help text missing" \
  120 \
  "$real_tool" chengc --help

run_logged_expect "$real_fixed0_out" \
  "real launcher fixed0 gate command failed" \
  'verify_backend_stage1_fixed0_envs ok' \
  "real launcher fixed0 gate failed" \
  120 \
  run_gate_sanitized "$real_tool" verify_backend_stage1_fixed0_envs

run_logged_expect "$real_strlit_out" \
  "real launcher string literal gate command failed" \
  'verify_backend_string_literal_regression ok' \
  "real launcher string literal gate failed" \
  160 \
  env \
    BACKEND_STRING_LITERAL_TIMEOUT="${BACKEND_STRING_LITERAL_TIMEOUT:-60}" \
    BACKEND_LINKER=system \
    BACKEND_MM_LINKER=system \
    BACKEND_NO_RUNTIME_C=0 \
    BACKEND_DIRECT_EXE=0 \
    BACKEND_LINKERLESS_INMEM=0 \
    BACKEND_FAST_FALLBACK_ALLOW=0 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    "$real_tool" verify_backend_string_literal_regression

run_logged_expect "$real_strformat_out" \
  "real launcher std strformat gate command failed" \
  'verify_std_strformat ok' \
  "real launcher std strformat gate failed" \
  160 \
  run_gate_sanitized "$real_tool" verify_std_strformat

run_logged_expect "$real_output_safety_out" \
  "real launcher backend default output safety gate command failed" \
  'verify_backend_default_output_safety ok' \
  "real launcher backend default output safety gate failed" \
  160 \
  run_gate_sanitized "$real_tool" verify_backend_default_output_safety

run_logged_expect "$real_new_expr_out" \
  "real launcher new-expr surface gate command failed" \
  'verify_new_expr_surface ok' \
  "real launcher new-expr surface gate failed" \
  160 \
  run_gate_sanitized "$real_tool" verify_new_expr_surface

run_logged_expect "$real_currentsrc_proof_out" \
  "real launcher current-source proof gate command failed" \
  'verify_backend_selfhost_currentsrc_proof ok' \
  "real launcher current-source proof gate failed" \
  200 \
  run_currentsrc_proof_gate "$real_tool" "$real_currentsrc_proof_out" 1

run_logged_expect "$real_mir_borrow_out" \
  "real launcher mir_borrow gate command failed" \
  'verify_backend_mir_borrow ok' \
  "real launcher mir_borrow gate failed" \
  200 \
  run_mir_borrow_gate "$real_tool" "$real_mir_borrow_out"

run_logged_expect "$real_core_backend_prod_help_out" \
  "real launcher core backend_prod_closure --help failed" \
  'Usage:' \
  "real launcher core backend_prod_closure help text missing" \
  120 \
  env TOOLING_REAL_CANDIDATE=core "$real_tool" backend_prod_closure --help

run_logged_expect "$install_stdout" \
  "global install command failed" \
  "" \
  "" \
  160 \
  "$global_bin" install --dir:"$install_dir" --bin:"$global_bin" --manifest:"$install_manifest" --force
for link in backend_prod_closure verify_backend_closedloop cheng_tooling chengc; do
  if [ ! -x "$install_dir/$link" ]; then
    echo "[verify_tooling_cmdline] missing multicall link: $install_dir/$link" 1>&2
    exit 1
  fi
done
if [ ! -x "$install_dir/verify_backend_selfhost_currentsrc_proof" ]; then
  echo "[verify_tooling_cmdline] missing multicall link: $install_dir/verify_backend_selfhost_currentsrc_proof" 1>&2
  exit 1
fi
if [ ! -x "$install_dir/verify_backend_mir_borrow" ]; then
  echo "[verify_tooling_cmdline] missing multicall link: $install_dir/verify_backend_mir_borrow" 1>&2
  exit 1
fi
if [ ! -x "$install_dir/verify_new_expr_surface" ]; then
  echo "[verify_tooling_cmdline] missing multicall link: $install_dir/verify_new_expr_surface" 1>&2
  exit 1
fi
if [ ! -x "$install_dir/verify_backend_string_abi_contract" ]; then
  echo "[verify_tooling_cmdline] missing multicall link: $install_dir/verify_backend_string_abi_contract" 1>&2
  exit 1
fi
if [ ! -x "$install_dir/verify_backend_dot_lowering_contract" ]; then
  echo "[verify_tooling_cmdline] missing multicall link: $install_dir/verify_backend_dot_lowering_contract" 1>&2
  exit 1
fi
if ! rg -q '^backend_prod_closure\t' "$install_manifest"; then
  echo "[verify_tooling_cmdline] install manifest missing backend_prod_closure entry" 1>&2
  exit 1
fi
if ! rg -q '^chengc\t' "$install_manifest"; then
  echo "[verify_tooling_cmdline] install manifest missing chengc entry" 1>&2
  exit 1
fi

run_logged_expect "$help_out_multicall" \
  "multicall backend_prod_closure --help failed" \
  'Usage:' \
  "multicall backend_prod_closure help text missing" \
  120 \
  "$install_dir/backend_prod_closure" --help

run_logged_expect "$help_out_multicall_chengc" \
  "multicall chengc --help failed" \
  'Usage:' \
  "multicall chengc help text missing" \
  120 \
  "$install_dir/chengc" --help

run_logged_expect "$help_out_multicall_currentsrc_proof" \
  "multicall verify_backend_selfhost_currentsrc_proof --help failed" \
  'Usage:' \
  "multicall verify_backend_selfhost_currentsrc_proof help text missing" \
  120 \
  "$install_dir/verify_backend_selfhost_currentsrc_proof" --help

run_logged_expect "$help_out_multicall_mir_borrow" \
  "multicall verify_backend_mir_borrow --help failed" \
  'Usage:' \
  "multicall verify_backend_mir_borrow help text missing" \
  120 \
  "$install_dir/verify_backend_mir_borrow" --help

run_logged_expect "$help_out_multicall_new_expr" \
  "multicall verify_new_expr_surface --help failed" \
  'Usage:' \
  "multicall verify_new_expr_surface help text missing" \
  120 \
  "$install_dir/verify_new_expr_surface" --help

run_logged_expect "$help_out_multicall_string_abi" \
  "multicall verify_backend_string_abi_contract --help failed" \
  'Usage:' \
  "multicall verify_backend_string_abi_contract help text missing" \
  120 \
  "$install_dir/verify_backend_string_abi_contract" --help

run_logged_expect "$help_out_multicall_dot_lowering" \
  "multicall verify_backend_dot_lowering_contract --help failed" \
  'Usage:' \
  "multicall verify_backend_dot_lowering_contract help text missing" \
  120 \
  "$install_dir/verify_backend_dot_lowering_contract" --help

if [ ! -x "$bundle_new_expr_bin" ]; then
  echo "[verify_tooling_cmdline] missing bundled script-backed gate: $bundle_new_expr_bin" 1>&2
  exit 1
fi
if [ ! -x "$bundle_currentsrc_proof_bin" ]; then
  echo "[verify_tooling_cmdline] missing bundled script-backed gate: $bundle_currentsrc_proof_bin" 1>&2
  exit 1
fi
if [ ! -x "$bundle_mir_borrow_bin" ]; then
  echo "[verify_tooling_cmdline] missing bundled script-backed gate: $bundle_mir_borrow_bin" 1>&2
  exit 1
fi
if [ ! -x "$bundle_string_abi_bin" ]; then
  echo "[verify_tooling_cmdline] missing bundled script-backed gate: $bundle_string_abi_bin" 1>&2
  exit 1
fi
if [ ! -x "$bundle_dot_lowering_bin" ]; then
  echo "[verify_tooling_cmdline] missing bundled script-backed gate: $bundle_dot_lowering_bin" 1>&2
  exit 1
fi
if [ ! -f "$bundle_manifest" ]; then
  echo "[verify_tooling_cmdline] missing bundle manifest: $bundle_manifest" 1>&2
  exit 1
fi
if ! rg -q '^verify_new_expr_surface\t' "$bundle_manifest"; then
  echo "[verify_tooling_cmdline] bundle manifest missing verify_new_expr_surface" 1>&2
  sed -n '1,160p' "$bundle_manifest" 1>&2 || true
  exit 1
fi
if ! rg -q '^verify_backend_selfhost_currentsrc_proof\t' "$bundle_manifest"; then
  echo "[verify_tooling_cmdline] bundle manifest missing verify_backend_selfhost_currentsrc_proof" 1>&2
  sed -n '1,160p' "$bundle_manifest" 1>&2 || true
  exit 1
fi
if ! rg -q '^verify_backend_mir_borrow\t' "$bundle_manifest"; then
  echo "[verify_tooling_cmdline] bundle manifest missing verify_backend_mir_borrow" 1>&2
  sed -n '1,200p' "$bundle_manifest" 1>&2 || true
  exit 1
fi
if ! rg -q '^verify_backend_string_abi_contract\t' "$bundle_manifest"; then
  echo "[verify_tooling_cmdline] bundle manifest missing verify_backend_string_abi_contract" 1>&2
  sed -n '1,200p' "$bundle_manifest" 1>&2 || true
  exit 1
fi
if ! rg -q '^verify_backend_dot_lowering_contract\t' "$bundle_manifest"; then
  echo "[verify_tooling_cmdline] bundle manifest missing verify_backend_dot_lowering_contract" 1>&2
  sed -n '1,200p' "$bundle_manifest" 1>&2 || true
  exit 1
fi
run_logged_expect "$bundle_new_expr_help_out" \
  "bundled verify_new_expr_surface --help failed" \
  'Usage:' \
  "bundled verify_new_expr_surface help text missing" \
  120 \
  "$bundle_new_expr_bin" --help

run_logged_expect "$bundle_currentsrc_proof_help_out" \
  "bundled verify_backend_selfhost_currentsrc_proof --help failed" \
  'Usage:' \
  "bundled verify_backend_selfhost_currentsrc_proof help text missing" \
  120 \
  "$bundle_currentsrc_proof_bin" --help

run_logged_expect "$bundle_mir_borrow_help_out" \
  "bundled verify_backend_mir_borrow --help failed" \
  'Usage:' \
  "bundled verify_backend_mir_borrow help text missing" \
  120 \
  "$bundle_mir_borrow_bin" --help

run_logged_expect "$bundle_string_abi_help_out" \
  "bundled verify_backend_string_abi_contract --help failed" \
  'Usage:' \
  "bundled verify_backend_string_abi_contract help text missing" \
  120 \
  "$bundle_string_abi_bin" --help

run_logged_expect "$bundle_dot_lowering_help_out" \
  "bundled verify_backend_dot_lowering_contract --help failed" \
  'Usage:' \
  "bundled verify_backend_dot_lowering_contract help text missing" \
  120 \
  "$bundle_dot_lowering_bin" --help

{
  echo "tooling_cmdline_runner=ok"
  echo "repo_tool=$repo_tool"
  echo "repo_verify_help_output=$final_out_dir/$(basename -- "$repo_verify_help_out")"
  echo "repo_backend_prod_publish_alias_help_output=$final_out_dir/$(basename -- "$repo_backend_prod_publish_alias_help_out")"
  echo "repo_selfhost_100ms_alias_help_output=$final_out_dir/$(basename -- "$repo_selfhost_100ms_alias_help_out")"
  echo "repo_chengc_help_output=$final_out_dir/$(basename -- "$repo_chengc_help_out")"
  echo "repo_fixed0_output=$final_out_dir/$(basename -- "$repo_fixed0_out")"
  echo "repo_string_literal_output=$final_out_dir/$(basename -- "$repo_strlit_out")"
  echo "repo_std_strformat_output=$final_out_dir/$(basename -- "$repo_strformat_out")"
  echo "repo_backend_default_output_safety_output=$final_out_dir/$(basename -- "$repo_output_safety_out")"
  echo "repo_verify_new_expr_surface_output=$final_out_dir/$(basename -- "$repo_new_expr_out")"
  echo "repo_backend_selfhost_currentsrc_proof_output=$final_out_dir/$(basename -- "$repo_currentsrc_proof_out")"
  echo "repo_backend_mir_borrow_output=$final_out_dir/$(basename -- "$repo_mir_borrow_out")"
  echo "repo_backend_string_abi_contract_output=$final_out_dir/$(basename -- "$repo_string_abi_out")"
  echo "repo_backend_dot_lowering_contract_output=$final_out_dir/$(basename -- "$repo_dot_lowering_out")"
  echo "global_verify_help_output=$final_out_dir/$(basename -- "$tool_verify_help_out")"
  echo "global_backend_prod_publish_alias_help_output=$final_out_dir/$(basename -- "$tool_backend_prod_publish_alias_help_out")"
  echo "global_selfhost_100ms_alias_help_output=$final_out_dir/$(basename -- "$tool_selfhost_100ms_alias_help_out")"
  echo "global_chengc_help_output=$final_out_dir/$(basename -- "$tool_chengc_help_out")"
  echo "global_fixed0_output=$final_out_dir/$(basename -- "$tool_fixed0_out")"
  echo "global_string_literal_output=$final_out_dir/$(basename -- "$tool_strlit_out")"
  echo "global_std_strformat_output=$final_out_dir/$(basename -- "$tool_strformat_out")"
  echo "global_backend_default_output_safety_output=$final_out_dir/$(basename -- "$tool_output_safety_out")"
  echo "global_verify_new_expr_surface_output=$final_out_dir/$(basename -- "$tool_new_expr_out")"
  echo "global_backend_selfhost_currentsrc_proof_output=$final_out_dir/$(basename -- "$tool_currentsrc_proof_out")"
  echo "global_backend_mir_borrow_output=$final_out_dir/$(basename -- "$tool_mir_borrow_out")"
  echo "global_backend_string_abi_contract_output=$final_out_dir/$(basename -- "$tool_string_abi_out")"
  echo "global_backend_dot_lowering_contract_output=$final_out_dir/$(basename -- "$tool_dot_lowering_out")"
  echo "real_list_output=$final_out_dir/$(basename -- "$real_list_out")"
  echo "real_primary_bin=$real_primary_bin"
  echo "real_primary_template=$real_primary_src"
  echo "real_primary_list_output=$final_out_dir/$(basename -- "$real_primary_list_out")"
  echo "real_primary_verify_help_output=$final_out_dir/$(basename -- "$real_primary_verify_help_out")"
  echo "real_primary_chengc_help_output=$final_out_dir/$(basename -- "$real_primary_chengc_help_out")"
  echo "real_primary_fixed0_output=$final_out_dir/$(basename -- "$real_primary_fixed0_out")"
  echo "real_primary_string_literal_output=$final_out_dir/$(basename -- "$real_primary_strlit_out")"
  echo "real_primary_std_strformat_output=$final_out_dir/$(basename -- "$real_primary_strformat_out")"
  echo "real_primary_backend_default_output_safety_output=$final_out_dir/$(basename -- "$real_primary_output_safety_out")"
  echo "real_primary_verify_new_expr_surface_output=$final_out_dir/$(basename -- "$real_primary_new_expr_out")"
  echo "real_primary_backend_selfhost_currentsrc_proof_output=$final_out_dir/$(basename -- "$real_primary_currentsrc_proof_out")"
  echo "real_primary_backend_mir_borrow_output=$final_out_dir/$(basename -- "$real_primary_mir_borrow_out")"
  echo "real_template=$real_tool_src"
  echo "real_verify_help_output=$final_out_dir/$(basename -- "$real_verify_help_out")"
  echo "real_chengc_help_output=$final_out_dir/$(basename -- "$real_chengc_help_out")"
  echo "real_fixed0_output=$final_out_dir/$(basename -- "$real_fixed0_out")"
  echo "real_string_literal_output=$final_out_dir/$(basename -- "$real_strlit_out")"
  echo "real_std_strformat_output=$final_out_dir/$(basename -- "$real_strformat_out")"
  echo "real_backend_default_output_safety_output=$final_out_dir/$(basename -- "$real_output_safety_out")"
  echo "real_verify_new_expr_surface_output=$final_out_dir/$(basename -- "$real_new_expr_out")"
  echo "real_backend_selfhost_currentsrc_proof_output=$final_out_dir/$(basename -- "$real_currentsrc_proof_out")"
  echo "real_backend_mir_borrow_output=$final_out_dir/$(basename -- "$real_mir_borrow_out")"
  echo "real_core_backend_prod_help_output=$final_out_dir/$(basename -- "$real_core_backend_prod_help_out")"
  echo "list_output=$final_out_dir/$(basename -- "$list_out")"
  echo "help_output=$final_out_dir/$(basename -- "$help_out")"
  echo "help_output_multicall=$final_out_dir/$(basename -- "$help_out_multicall")"
  echo "help_output_multicall_chengc=$final_out_dir/$(basename -- "$help_out_multicall_chengc")"
  echo "help_output_multicall_new_expr=$final_out_dir/$(basename -- "$help_out_multicall_new_expr")"
  echo "help_output_multicall_currentsrc_proof=$final_out_dir/$(basename -- "$help_out_multicall_currentsrc_proof")"
  echo "help_output_multicall_mir_borrow=$final_out_dir/$(basename -- "$help_out_multicall_mir_borrow")"
  echo "help_output_multicall_string_abi=$final_out_dir/$(basename -- "$help_out_multicall_string_abi")"
  echo "help_output_multicall_dot_lowering=$final_out_dir/$(basename -- "$help_out_multicall_dot_lowering")"
  echo "global_bin=$global_bin"
  echo "install_dir=$final_out_dir/$(basename -- "$install_dir")"
  echo "install_manifest=$final_out_dir/$(basename -- "$install_manifest")"
  echo "install_stdout=$final_out_dir/$(basename -- "$install_stdout")"
  echo "bundle_dir=$bundle_dir"
  echo "bundle_manifest=$bundle_manifest"
  echo "bundle_new_expr_help_output=$final_out_dir/$(basename -- "$bundle_new_expr_help_out")"
  echo "bundle_currentsrc_proof_help_output=$final_out_dir/$(basename -- "$bundle_currentsrc_proof_help_out")"
  echo "bundle_mir_borrow_help_output=$final_out_dir/$(basename -- "$bundle_mir_borrow_help_out")"
  echo "bundle_string_abi_help_output=$final_out_dir/$(basename -- "$bundle_string_abi_help_out")"
  echo "bundle_dot_lowering_help_output=$final_out_dir/$(basename -- "$bundle_dot_lowering_help_out")"
  echo "currentsrc_proof_out_dir=$final_out_dir/$(basename -- "$currentsrc_proof_out_dir")"
} >"$report"

publish_dir="$(mktemp -d "$artifacts_dir/tooling_cmdline.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$work_dir" "$publish_dir"
work_dir=""
rm -rf "$final_out_dir"
mv "$publish_dir" "$final_out_dir"
publish_dir=""

echo "verify_tooling_cmdline ok"
exit 0
