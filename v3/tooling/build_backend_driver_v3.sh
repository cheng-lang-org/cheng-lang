#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
bridge_env="$root/artifacts/v3_bootstrap/bootstrap.env"
out_dir="$root/artifacts/v3_backend_driver"
out_path="$out_dir/cheng"
log_path="$out_dir/build_backend_driver_v3.log"
report_path="$out_dir/build_backend_driver_v3.report.txt"
contract_log="$out_dir/build_backend_driver_v3.contract.txt"
status_log="$out_dir/build_backend_driver_v3.status.txt"
plan_log="$out_dir/build_backend_driver_v3.plan.txt"

mkdir -p "$out_dir"

if [ ! -f "$bridge_env" ]; then
  sh "$root/v3/tooling/bootstrap_bridge_v3.sh"
fi

. "$bridge_env"

if [ ! -x "${V3_BOOTSTRAP_STAGE2:-}" ]; then
  echo "v3 backend driver: missing stage2 compiler: ${V3_BOOTSTRAP_STAGE2:-}" >&2
  exit 1
fi

if [ ! -f "${V3_BOOTSTRAP_STAGE1_SOURCE:-}" ]; then
  echo "v3 backend driver: missing stage1 source: ${V3_BOOTSTRAP_STAGE1_SOURCE:-}" >&2
  exit 1
fi

build_rc=0
set +e
"$V3_BOOTSTRAP_STAGE2" compile-bootstrap \
  --in:"$V3_BOOTSTRAP_STAGE1_SOURCE" \
  --out:"$out_path" \
  --report-out:"$out_dir/build_backend_driver_v3.compile.report.txt" \
  >"$log_path" 2>&1
build_rc="$?"
set -e

if [ "$build_rc" -ne 0 ]; then
  echo "v3 backend driver: stage2 compile-bootstrap failed rc=$build_rc log=$log_path" >&2
  tail -n 80 "$log_path" >&2 || true
  exit 1
fi

if [ ! -x "$out_path" ]; then
  echo "v3 backend driver: build returned success but no executable was produced: $out_path" >&2
  exit 1
fi

if ! "$out_path" print-contract --in:"$V3_BOOTSTRAP_STAGE1_SOURCE" >"$contract_log" 2>&1; then
  echo "v3 backend driver: print-contract failed log=$contract_log" >&2
  tail -n 80 "$contract_log" >&2 || true
  exit 1
fi

if ! "$out_path" status >"$status_log" 2>&1; then
  echo "v3 backend driver: built output is still bootstrap-only, missing ordinary status command: $out_path" >&2
  tail -n 80 "$status_log" >&2 || true
  exit 1
fi

if ! "$out_path" print-build-plan >"$plan_log" 2>&1; then
  echo "v3 backend driver: built output is still bootstrap-only, missing ordinary build-plan command: $out_path" >&2
  tail -n 80 "$plan_log" >&2 || true
  exit 1
fi

if [ -x "${V3_BOOTSTRAP_STAGE3:-}" ] && ! "$V3_BOOTSTRAP_STAGE3" print-contract --in:"$V3_BOOTSTRAP_STAGE1_SOURCE" \
  >"$out_dir/reference_stage3.contract.txt" 2>&1; then
  echo "v3 backend driver: stage3 print-contract failed" >&2
  tail -n 80 "$out_dir/reference_stage3.contract.txt" >&2 || true
  exit 1
fi

if [ -x "${V3_BOOTSTRAP_STAGE3:-}" ] && ! cmp -s "$contract_log" "$out_dir/reference_stage3.contract.txt"; then
  echo "v3 backend driver: contract drift vs stage3: $out_path" >&2
  exit 1
fi

cat >"$report_path" <<EOF
target=${V3_TARGET:-arm64-apple-darwin}
bootstrap_kind=${V3_BOOTSTRAP_KIND:-v3_seed}
compiler_class=ordinary_compiler
stage0_compiler=${V3_BOOTSTRAP_STAGE0:-}
stage1_compiler=${V3_BOOTSTRAP_STAGE1:-}
stage2_compiler=$V3_BOOTSTRAP_STAGE2
stage3_compiler=${V3_BOOTSTRAP_STAGE3:-}
bootstrap_contract_source=$V3_BOOTSTRAP_STAGE1_SOURCE
planned_entry_source=${V3_COMPILER_ENTRY_SOURCE:-}
planned_runtime_source=${V3_COMPILER_RUNTIME_SOURCE:-}
planned_request_source=${V3_COMPILER_REQUEST_SOURCE:-}
materialized_source=$V3_BOOTSTRAP_STAGE1_SOURCE
output=$out_path
contract_log=$contract_log
status_log=$status_log
plan_log=$plan_log
log=$log_path
EOF

echo "v3 backend driver ok: $out_path"
