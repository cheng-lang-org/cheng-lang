#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_backend_metering_phase_barrier] $1" >&2
  exit 1
}

line_no() {
  file="$1"
  pattern="$2"
  rg -n --no-messages "$pattern" "$file" | head -n 1 | cut -d: -f1
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

uir_opt_file="src/backend/uir/uir_opt.cheng"
builder_policy_file="src/backend/uir/uir_internal/uir_core_builder_policy_contract.cheng"
metering_sdk_file="src/decentralized/metering_sdk.cheng"

require_marker "$uir_opt_file" 'Opt2 default pipeline: noalias -> ssu -> opt2 -> cleanup -> egraph\.' 'opt2_pipeline_comment'
require_marker "$uir_opt_file" 'uirRunEGraphRewrite\(' 'egraph_pass'
require_marker "$uir_opt_file" 'uirProfileStep\(profileState, "uir_opt2.cost_model' 'cost_model_step'
require_marker "$uir_opt_file" 'uirProfilePass\(profileState, "uir_simd.vectorize", vecChanged\)' 'simd_vectorize_pass'
require_marker "$builder_policy_file" 'getEnv "METERING_PLUGIN"' 'metering_plugin_env_switch'
require_marker "$builder_policy_file" 'fn uirCoreApplyAstPluginHooks' 'ast_plugin_hook'
require_marker "$builder_policy_file" 'fn uirCoreApplyMirPluginHooks' 'mir_plugin_hook'
require_marker "$metering_sdk_file" 'template withMetering\(' 'sdk_with_metering'
require_marker "$metering_sdk_file" 'template withMeteringIo\(' 'sdk_with_metering_io'

if rg -q 'METERING_PLUGIN|withMetering|metering_sdk|metering\.default' "$uir_opt_file"; then
  fail "metering markers leaked into optimizer pipeline file: $uir_opt_file"
fi

egraph_line="$(line_no "$uir_opt_file" 'uirRunEGraphRewrite\(')"
cost_line="$(line_no "$uir_opt_file" 'uirProfileStep\(profileState, "uir_opt2.cost_model')"
simd_line="$(line_no "$uir_opt_file" 'uirProfilePass\(profileState, "uir_simd.vectorize", vecChanged\)')"
ast_hook_line="$(line_no "$builder_policy_file" 'fn uirCoreApplyAstPluginHooks')"
mir_hook_line="$(line_no "$builder_policy_file" 'fn uirCoreApplyMirPluginHooks')"

for v in "$egraph_line" "$cost_line" "$simd_line" "$ast_hook_line" "$mir_hook_line"; do
  case "$v" in
    ''|*[!0-9]*)
      fail "failed to resolve required phase markers"
      ;;
  esac
done

if [ "$egraph_line" -ge "$cost_line" ]; then
  fail "egraph pass must execute before cost model scheduling"
fi
if [ "$cost_line" -ge "$simd_line" ]; then
  fail "cost model scheduling must execute before SIMD/vectorize pass"
fi
if [ "$ast_hook_line" -ge "$mir_hook_line" ]; then
  fail "AST plugin hook must be declared before MIR plugin hook"
fi

out_dir="artifacts/backend_metering_phase_barrier"
mkdir -p "$out_dir"
report="$out_dir/backend_metering_phase_barrier.report.txt"
{
  echo "verify_backend_metering_phase_barrier report"
  echo "status=ok"
  echo "egraph_line=$egraph_line"
  echo "cost_line=$cost_line"
  echo "simd_line=$simd_line"
  echo "ast_hook_line=$ast_hook_line"
  echo "mir_hook_line=$mir_hook_line"
  echo "metering_opt_pipeline_pure=1"
} >"$report"

echo "verify_backend_metering_phase_barrier ok"
