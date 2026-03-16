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
  if [ "$publish_dir" != "" ] && [ -e "$publish_dir" ]; then
    rm -rf "$publish_dir" 2>/dev/null || true
  fi
  if [ "$work_dir" != "" ] && [ -e "$work_dir" ]; then
    rm -rf "$work_dir" 2>/dev/null || true
  fi
}
trap cleanup EXIT INT TERM
out_dir="$work_dir"

list_out="$out_dir/tooling_cmdline.list.txt"
help_out="$out_dir/tooling_cmdline.backend_prod_closure.help.txt"
repo_chengc_help_out="$out_dir/tooling_cmdline.repo_chengc.help.txt"
tool_chengc_help_out="$out_dir/tooling_cmdline.tool_chengc.help.txt"
help_out_multicall="$out_dir/tooling_cmdline.backend_prod_closure.multicall.help.txt"
help_out_multicall_chengc="$out_dir/tooling_cmdline.chengc.multicall.help.txt"
repo_verify_help_out="$out_dir/tooling_cmdline.repo_verify.help.txt"
repo_fixed0_out="$out_dir/tooling_cmdline.repo_fixed0.txt"
repo_strlit_out="$out_dir/tooling_cmdline.repo_string_literal.txt"
repo_strformat_out="$out_dir/tooling_cmdline.repo_std_strformat.txt"
repo_output_safety_out="$out_dir/tooling_cmdline.repo_backend_default_output_safety.txt"
tool_verify_help_out="$out_dir/tooling_cmdline.tool_verify.help.txt"
tool_fixed0_out="$out_dir/tooling_cmdline.tool_fixed0.txt"
tool_strlit_out="$out_dir/tooling_cmdline.tool_string_literal.txt"
tool_strformat_out="$out_dir/tooling_cmdline.tool_std_strformat.txt"
tool_output_safety_out="$out_dir/tooling_cmdline.tool_backend_default_output_safety.txt"
real_list_out="$out_dir/tooling_cmdline.real.list.txt"
real_primary_list_out="$out_dir/tooling_cmdline.real_primary.list.txt"
real_verify_help_out="$out_dir/tooling_cmdline.real_verify.help.txt"
real_chengc_help_out="$out_dir/tooling_cmdline.real_chengc.help.txt"
real_fixed0_out="$out_dir/tooling_cmdline.real_fixed0.txt"
real_strlit_out="$out_dir/tooling_cmdline.real_string_literal.txt"
real_strformat_out="$out_dir/tooling_cmdline.real_std_strformat.txt"
real_output_safety_out="$out_dir/tooling_cmdline.real_backend_default_output_safety.txt"
real_core_backend_prod_help_out="$out_dir/tooling_cmdline.real_core_backend_prod_closure.help.txt"
real_primary_verify_help_out="$out_dir/tooling_cmdline.real_primary_verify.help.txt"
real_primary_chengc_help_out="$out_dir/tooling_cmdline.real_primary_chengc.help.txt"
real_primary_fixed0_out="$out_dir/tooling_cmdline.real_primary_fixed0.txt"
real_primary_strlit_out="$out_dir/tooling_cmdline.real_primary_string_literal.txt"
real_primary_strformat_out="$out_dir/tooling_cmdline.real_primary_std_strformat.txt"
real_primary_output_safety_out="$out_dir/tooling_cmdline.real_primary_backend_default_output_safety.txt"
global_bin="$out_dir/cheng_tooling"
install_dir="$out_dir/bin"
install_manifest="$out_dir/tooling_cmdline.install_manifest.tsv"
install_stdout="$out_dir/tooling_cmdline.install.stdout.txt"
report="$out_dir/tooling_cmdline.report.txt"

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

sh "$repo_tool" verify --help >"$repo_verify_help_out" 2>&1
if ! rg -q 'Usage:' "$repo_verify_help_out"; then
  echo "[verify_tooling_cmdline] repo verify help text missing" 1>&2
  exit 1
fi

run_gate_sanitized sh "$repo_tool" verify_backend_stage1_fixed0_envs >"$repo_fixed0_out" 2>&1
if ! rg -q 'verify_backend_stage1_fixed0_envs ok' "$repo_fixed0_out"; then
  echo "[verify_tooling_cmdline] repo fixed0 gate failed" 1>&2
  sed -n '1,120p' "$repo_fixed0_out" 1>&2 || true
  exit 1
fi

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
  sh "$repo_tool" verify_backend_string_literal_regression >"$repo_strlit_out" 2>&1
if ! rg -q 'verify_backend_string_literal_regression ok' "$repo_strlit_out"; then
  echo "[verify_tooling_cmdline] repo string literal gate failed" 1>&2
  sed -n '1,160p' "$repo_strlit_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized sh "$repo_tool" verify_std_strformat >"$repo_strformat_out" 2>&1
if ! rg -q 'verify_std_strformat ok' "$repo_strformat_out"; then
  echo "[verify_tooling_cmdline] repo std strformat gate failed" 1>&2
  sed -n '1,160p' "$repo_strformat_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized sh "$repo_tool" verify_backend_default_output_safety >"$repo_output_safety_out" 2>&1
if ! rg -q 'verify_backend_default_output_safety ok' "$repo_output_safety_out"; then
  echo "[verify_tooling_cmdline] repo backend default output safety gate failed" 1>&2
  sed -n '1,160p' "$repo_output_safety_out" 1>&2 || true
  exit 1
fi

sh "$repo_tool" chengc --help >"$repo_chengc_help_out" 2>&1
if ! rg -q 'Usage:' "$repo_chengc_help_out"; then
  echo "[verify_tooling_cmdline] repo chengc help text missing" 1>&2
  exit 1
fi

TOOLING_FORCE_BUILD=1 TOOLING_LINKER=system "$tool" list >"$list_out"
for id in backend_prod_closure verify_backend_closedloop chengc verify_tooling_cmdline; do
  if ! rg -qx "$id" "$list_out"; then
    echo "[verify_tooling_cmdline] missing id in list output: $id" 1>&2
    exit 1
  fi
done

TOOLING_LINKER=system "$tool" backend_prod_closure --help >"$help_out"
if ! rg -q 'Usage:' "$help_out"; then
  echo "[verify_tooling_cmdline] backend_prod_closure help text missing" 1>&2
  exit 1
fi

global_bin="${TOOLING_GLOBAL_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
if [ ! -x "$global_bin" ]; then
  global_bin="$tool"
fi
if [ ! -x "$global_bin" ]; then
  echo "[verify_tooling_cmdline] missing runnable global tool binary: $global_bin" 1>&2
  exit 1
fi
"$global_bin" list >/dev/null

"$global_bin" verify --help >"$tool_verify_help_out" 2>&1
if ! rg -q 'Usage:' "$tool_verify_help_out"; then
  echo "[verify_tooling_cmdline] global verify help text missing" 1>&2
  exit 1
fi

"$global_bin" chengc --help >"$tool_chengc_help_out" 2>&1
if ! rg -q 'Usage:' "$tool_chengc_help_out"; then
  echo "[verify_tooling_cmdline] global chengc help text missing" 1>&2
  exit 1
fi

run_gate_sanitized "$global_bin" verify_backend_stage1_fixed0_envs >"$tool_fixed0_out" 2>&1
if ! rg -q 'verify_backend_stage1_fixed0_envs ok' "$tool_fixed0_out"; then
  echo "[verify_tooling_cmdline] global fixed0 gate failed" 1>&2
  sed -n '1,120p' "$tool_fixed0_out" 1>&2 || true
  exit 1
fi

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
  "$global_bin" verify_backend_string_literal_regression >"$tool_strlit_out" 2>&1
if ! rg -q 'verify_backend_string_literal_regression ok' "$tool_strlit_out"; then
  echo "[verify_tooling_cmdline] global string literal gate failed" 1>&2
  sed -n '1,160p' "$tool_strlit_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized "$global_bin" verify_std_strformat >"$tool_strformat_out" 2>&1
if ! rg -q 'verify_std_strformat ok' "$tool_strformat_out"; then
  echo "[verify_tooling_cmdline] global std strformat gate failed" 1>&2
  sed -n '1,160p' "$tool_strformat_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized "$global_bin" verify_backend_default_output_safety >"$tool_output_safety_out" 2>&1
if ! rg -q 'verify_backend_default_output_safety ok' "$tool_output_safety_out"; then
  echo "[verify_tooling_cmdline] global backend default output safety gate failed" 1>&2
  sed -n '1,160p' "$tool_output_safety_out" 1>&2 || true
  exit 1
fi

"$real_tool" list >"$real_list_out"
for id in chengc backend_prod_closure verify; do
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

"$real_primary_bin" list >"$real_primary_list_out"
for id in chengc backend_prod_closure verify; do
  if ! rg -qx "$id" "$real_primary_list_out"; then
    echo "[verify_tooling_cmdline] real primary binary missing id in list output: $id" 1>&2
    exit 1
  fi
done

"$real_primary_bin" verify --help >"$real_primary_verify_help_out" 2>&1
if ! rg -q 'Usage:' "$real_primary_verify_help_out"; then
  echo "[verify_tooling_cmdline] real primary binary verify help text missing" 1>&2
  exit 1
fi

"$real_primary_bin" chengc --help >"$real_primary_chengc_help_out" 2>&1
if ! rg -q 'Usage:|compile options:' "$real_primary_chengc_help_out"; then
  echo "[verify_tooling_cmdline] real primary binary chengc help text missing" 1>&2
  exit 1
fi

run_gate_sanitized "$real_primary_bin" verify_backend_stage1_fixed0_envs >"$real_primary_fixed0_out" 2>&1
if ! rg -q 'verify_backend_stage1_fixed0_envs ok' "$real_primary_fixed0_out"; then
  echo "[verify_tooling_cmdline] real primary binary fixed0 gate failed" 1>&2
  sed -n '1,120p' "$real_primary_fixed0_out" 1>&2 || true
  exit 1
fi

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
  "$real_primary_bin" verify_backend_string_literal_regression >"$real_primary_strlit_out" 2>&1
if ! rg -q 'verify_backend_string_literal_regression ok' "$real_primary_strlit_out"; then
  echo "[verify_tooling_cmdline] real primary binary string literal gate failed" 1>&2
  sed -n '1,160p' "$real_primary_strlit_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized "$real_primary_bin" verify_std_strformat >"$real_primary_strformat_out" 2>&1
if ! rg -q 'verify_std_strformat ok' "$real_primary_strformat_out"; then
  echo "[verify_tooling_cmdline] real primary binary std strformat gate failed" 1>&2
  sed -n '1,160p' "$real_primary_strformat_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized "$real_primary_bin" verify_backend_default_output_safety >"$real_primary_output_safety_out" 2>&1
if ! rg -q 'verify_backend_default_output_safety ok' "$real_primary_output_safety_out"; then
  echo "[verify_tooling_cmdline] real primary binary backend default output safety gate failed" 1>&2
  sed -n '1,160p' "$real_primary_output_safety_out" 1>&2 || true
  exit 1
fi

"$real_tool" verify --help >"$real_verify_help_out" 2>&1
if ! rg -q 'Usage:' "$real_verify_help_out"; then
  echo "[verify_tooling_cmdline] real launcher verify help text missing" 1>&2
  exit 1
fi

"$real_tool" chengc --help >"$real_chengc_help_out" 2>&1
if ! rg -q 'Usage:|compile options:' "$real_chengc_help_out"; then
  echo "[verify_tooling_cmdline] real launcher chengc help text missing" 1>&2
  exit 1
fi

run_gate_sanitized "$real_tool" verify_backend_stage1_fixed0_envs >"$real_fixed0_out" 2>&1
if ! rg -q 'verify_backend_stage1_fixed0_envs ok' "$real_fixed0_out"; then
  echo "[verify_tooling_cmdline] real launcher fixed0 gate failed" 1>&2
  sed -n '1,120p' "$real_fixed0_out" 1>&2 || true
  exit 1
fi

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
  "$real_tool" verify_backend_string_literal_regression >"$real_strlit_out" 2>&1
if ! rg -q 'verify_backend_string_literal_regression ok' "$real_strlit_out"; then
  echo "[verify_tooling_cmdline] real launcher string literal gate failed" 1>&2
  sed -n '1,160p' "$real_strlit_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized "$real_tool" verify_std_strformat >"$real_strformat_out" 2>&1
if ! rg -q 'verify_std_strformat ok' "$real_strformat_out"; then
  echo "[verify_tooling_cmdline] real launcher std strformat gate failed" 1>&2
  sed -n '1,160p' "$real_strformat_out" 1>&2 || true
  exit 1
fi

run_gate_sanitized "$real_tool" verify_backend_default_output_safety >"$real_output_safety_out" 2>&1
if ! rg -q 'verify_backend_default_output_safety ok' "$real_output_safety_out"; then
  echo "[verify_tooling_cmdline] real launcher backend default output safety gate failed" 1>&2
  sed -n '1,160p' "$real_output_safety_out" 1>&2 || true
  exit 1
fi

TOOLING_REAL_CANDIDATE=core "$real_tool" backend_prod_closure --help >"$real_core_backend_prod_help_out" 2>&1
if ! rg -q 'Usage:' "$real_core_backend_prod_help_out"; then
  echo "[verify_tooling_cmdline] real launcher core backend_prod_closure help text missing" 1>&2
  exit 1
fi

"$global_bin" install --dir:"$install_dir" --bin:"$global_bin" --manifest:"$install_manifest" --force >"$install_stdout"
for link in backend_prod_closure verify_backend_closedloop cheng_tooling chengc; do
  if [ ! -x "$install_dir/$link" ]; then
    echo "[verify_tooling_cmdline] missing multicall link: $install_dir/$link" 1>&2
    exit 1
  fi
done
if ! rg -q '^backend_prod_closure\t' "$install_manifest"; then
  echo "[verify_tooling_cmdline] install manifest missing backend_prod_closure entry" 1>&2
  exit 1
fi
if ! rg -q '^chengc\t' "$install_manifest"; then
  echo "[verify_tooling_cmdline] install manifest missing chengc entry" 1>&2
  exit 1
fi

"$install_dir/backend_prod_closure" --help >"$help_out_multicall"
if ! rg -q 'Usage:' "$help_out_multicall"; then
  echo "[verify_tooling_cmdline] multicall backend_prod_closure help text missing" 1>&2
  exit 1
fi

"$install_dir/chengc" --help >"$help_out_multicall_chengc"
if ! rg -q 'Usage:' "$help_out_multicall_chengc"; then
  echo "[verify_tooling_cmdline] multicall chengc help text missing" 1>&2
  exit 1
fi

{
  echo "tooling_cmdline_runner=ok"
  echo "repo_tool=$repo_tool"
  echo "repo_verify_help_output=$final_out_dir/$(basename -- "$repo_verify_help_out")"
  echo "repo_chengc_help_output=$final_out_dir/$(basename -- "$repo_chengc_help_out")"
  echo "repo_fixed0_output=$final_out_dir/$(basename -- "$repo_fixed0_out")"
  echo "repo_string_literal_output=$final_out_dir/$(basename -- "$repo_strlit_out")"
  echo "repo_std_strformat_output=$final_out_dir/$(basename -- "$repo_strformat_out")"
  echo "repo_backend_default_output_safety_output=$final_out_dir/$(basename -- "$repo_output_safety_out")"
  echo "global_verify_help_output=$final_out_dir/$(basename -- "$tool_verify_help_out")"
  echo "global_chengc_help_output=$final_out_dir/$(basename -- "$tool_chengc_help_out")"
  echo "global_fixed0_output=$final_out_dir/$(basename -- "$tool_fixed0_out")"
  echo "global_string_literal_output=$final_out_dir/$(basename -- "$tool_strlit_out")"
  echo "global_std_strformat_output=$final_out_dir/$(basename -- "$tool_strformat_out")"
  echo "global_backend_default_output_safety_output=$final_out_dir/$(basename -- "$tool_output_safety_out")"
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
  echo "real_template=$real_tool_src"
  echo "real_verify_help_output=$final_out_dir/$(basename -- "$real_verify_help_out")"
  echo "real_chengc_help_output=$final_out_dir/$(basename -- "$real_chengc_help_out")"
  echo "real_fixed0_output=$final_out_dir/$(basename -- "$real_fixed0_out")"
  echo "real_string_literal_output=$final_out_dir/$(basename -- "$real_strlit_out")"
  echo "real_std_strformat_output=$final_out_dir/$(basename -- "$real_strformat_out")"
  echo "real_backend_default_output_safety_output=$final_out_dir/$(basename -- "$real_output_safety_out")"
  echo "real_core_backend_prod_help_output=$final_out_dir/$(basename -- "$real_core_backend_prod_help_out")"
  echo "list_output=$final_out_dir/$(basename -- "$list_out")"
  echo "help_output=$final_out_dir/$(basename -- "$help_out")"
  echo "help_output_multicall=$final_out_dir/$(basename -- "$help_out_multicall")"
  echo "help_output_multicall_chengc=$final_out_dir/$(basename -- "$help_out_multicall_chengc")"
  echo "global_bin=$global_bin"
  echo "install_dir=$final_out_dir/$(basename -- "$install_dir")"
  echo "install_manifest=$final_out_dir/$(basename -- "$install_manifest")"
  echo "install_stdout=$final_out_dir/$(basename -- "$install_stdout")"
} >"$report"

publish_dir="$(mktemp -d "$artifacts_dir/tooling_cmdline.publish.XXXXXX")"
rmdir "$publish_dir"
mv "$work_dir" "$publish_dir"
work_dir=""
rm -rf "$final_out_dir"
mv "$publish_dir" "$final_out_dir"
publish_dir=""

echo "verify_tooling_cmdline ok"
