#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out_dir="$root/artifacts/v3_bootstrap"
seed_source="$root/v3/bootstrap/cheng_v3_seed.c"
stage1_source="$root/v3/bootstrap/stage1_bootstrap.cheng"
compiler_entry_source="$root/v3/src/tooling/compiler_main.cheng"
compiler_runtime_source="$root/v3/src/tooling/compiler_runtime.cheng"
compiler_request_source="$root/v3/src/tooling/compiler_request.cheng"
stage0="$out_dir/cheng.stage0"
stage1="$out_dir/cheng.stage1"
stage2="$out_dir/cheng.stage2"
stage3="$out_dir/cheng.stage3"
env_path="$out_dir/bootstrap.env"
snapshot_path="$out_dir/bootstrap_snapshot.txt"
target="${V3_TARGET:-arm64-apple-darwin}"
cc_bin="${CC:-cc}"

stage0_build_log="$out_dir/cheng.stage0.build.log"
stage0_self_check_log="$out_dir/cheng.stage0.self-check.log"
stage1_build_log="$out_dir/cheng.stage1.compile.log"
stage1_self_check_log="$out_dir/cheng.stage1.self-check.log"
stage2_build_log="$out_dir/cheng.stage2.compile.log"
stage2_self_check_log="$out_dir/cheng.stage2.self-check.log"
stage3_build_log="$out_dir/cheng.stage3.compile.log"
stage3_self_check_log="$out_dir/cheng.stage3.self-check.log"
stage2_contract_log="$out_dir/cheng.stage2.contract.txt"
stage3_contract_log="$out_dir/cheng.stage3.contract.txt"

stage1_report="$out_dir/cheng.stage1.report.txt"
stage2_report="$out_dir/cheng.stage2.report.txt"
stage3_report="$out_dir/cheng.stage3.report.txt"

mkdir -p "$out_dir"

if [ ! -f "$seed_source" ]; then
  echo "v3 bootstrap bridge: missing seed source: $seed_source" >&2
  exit 1
fi

if [ ! -f "$stage1_source" ]; then
  echo "v3 bootstrap bridge: missing stage1 source: $stage1_source" >&2
  exit 1
fi

if [ ! -f "$compiler_entry_source" ]; then
  echo "v3 bootstrap bridge: missing compiler entry source: $compiler_entry_source" >&2
  exit 1
fi

if [ ! -f "$compiler_runtime_source" ]; then
  echo "v3 bootstrap bridge: missing compiler runtime source: $compiler_runtime_source" >&2
  exit 1
fi

if [ ! -f "$compiler_request_source" ]; then
  echo "v3 bootstrap bridge: missing compiler request source: $compiler_request_source" >&2
  exit 1
fi

if ! "$cc_bin" -std=c11 -O2 -Wall -Wextra -pedantic \
  "$seed_source" -o "$stage0" >"$stage0_build_log" 2>&1; then
  echo "v3 bootstrap bridge: stage0 seed build failed log=$stage0_build_log" >&2
  tail -n 80 "$stage0_build_log" >&2 || true
  exit 1
fi

if [ ! -x "$stage0" ]; then
  echo "v3 bootstrap bridge: missing compiled stage0 seed: $stage0" >&2
  exit 1
fi

if ! "$stage0" self-check --in:"$stage1_source" >"$stage0_self_check_log" 2>&1; then
  echo "v3 bootstrap bridge: stage0 self-check failed log=$stage0_self_check_log" >&2
  tail -n 80 "$stage0_self_check_log" >&2 || true
  exit 1
fi

if ! "$stage0" compile-bootstrap --in:"$stage1_source" --out:"$stage1" --report-out:"$stage1_report" \
  >"$stage1_build_log" 2>&1; then
  echo "v3 bootstrap bridge: stage1 compile failed log=$stage1_build_log" >&2
  tail -n 80 "$stage1_build_log" >&2 || true
  exit 1
fi

if ! "$stage1" self-check --in:"$stage1_source" >"$stage1_self_check_log" 2>&1; then
  echo "v3 bootstrap bridge: stage1 self-check failed log=$stage1_self_check_log" >&2
  tail -n 80 "$stage1_self_check_log" >&2 || true
  exit 1
fi

if ! "$stage1" compile-bootstrap --in:"$stage1_source" --out:"$stage2" --report-out:"$stage2_report" \
  >"$stage2_build_log" 2>&1; then
  echo "v3 bootstrap bridge: stage2 compile failed log=$stage2_build_log" >&2
  tail -n 80 "$stage2_build_log" >&2 || true
  exit 1
fi

if ! "$stage2" self-check --in:"$stage1_source" >"$stage2_self_check_log" 2>&1; then
  echo "v3 bootstrap bridge: stage2 self-check failed log=$stage2_self_check_log" >&2
  tail -n 80 "$stage2_self_check_log" >&2 || true
  exit 1
fi

if ! "$stage2" compile-bootstrap --in:"$stage1_source" --out:"$stage3" --report-out:"$stage3_report" \
  >"$stage3_build_log" 2>&1; then
  echo "v3 bootstrap bridge: stage3 compile failed log=$stage3_build_log" >&2
  tail -n 80 "$stage3_build_log" >&2 || true
  exit 1
fi

if ! "$stage3" self-check --in:"$stage1_source" >"$stage3_self_check_log" 2>&1; then
  echo "v3 bootstrap bridge: stage3 self-check failed log=$stage3_self_check_log" >&2
  tail -n 80 "$stage3_self_check_log" >&2 || true
  exit 1
fi

if ! "$stage2" print-contract --in:"$stage1_source" >"$stage2_contract_log" 2>&1; then
  echo "v3 bootstrap bridge: stage2 print-contract failed log=$stage2_contract_log" >&2
  tail -n 80 "$stage2_contract_log" >&2 || true
  exit 1
fi

if ! "$stage3" print-contract --in:"$stage1_source" >"$stage3_contract_log" 2>&1; then
  echo "v3 bootstrap bridge: stage3 print-contract failed log=$stage3_contract_log" >&2
  tail -n 80 "$stage3_contract_log" >&2 || true
  exit 1
fi

if ! cmp -s "$stage2_contract_log" "$stage3_contract_log"; then
  echo "v3 bootstrap bridge: fixed point mismatch stage2/stage3 contract drift" >&2
  exit 1
fi

cat >"$env_path" <<EOF
V3_ROOT=$root
V3_TARGET=$target
V3_BOOTSTRAP_KIND=v3_seed
V3_BOOTSTRAP_SEED_SOURCE=$seed_source
V3_BOOTSTRAP_STAGE1_SOURCE=$stage1_source
V3_COMPILER_ENTRY_SOURCE=$compiler_entry_source
V3_COMPILER_RUNTIME_SOURCE=$compiler_runtime_source
V3_COMPILER_REQUEST_SOURCE=$compiler_request_source
V3_BOOTSTRAP_STAGE0=$stage0
V3_BOOTSTRAP_STAGE1=$stage1
V3_BOOTSTRAP_STAGE2=$stage2
V3_BOOTSTRAP_STAGE3=$stage3
V3_BOOTSTRAP_ENV=$env_path
V3_BOOTSTRAP_SNAPSHOT=$snapshot_path
V3_BOOTSTRAP_STAGE1_REPORT=$stage1_report
V3_BOOTSTRAP_STAGE2_REPORT=$stage2_report
V3_BOOTSTRAP_STAGE3_REPORT=$stage3_report
EOF

cat >"$snapshot_path" <<EOF
target=$target
bootstrap_kind=v3_seed
seed_source=$seed_source
stage1_source=$stage1_source
compiler_entry_source=$compiler_entry_source
compiler_runtime_source=$compiler_runtime_source
compiler_request_source=$compiler_request_source
stage0=$stage0
stage1=$stage1
stage2=$stage2
stage3=$stage3
stage1_report=$stage1_report
stage2_report=$stage2_report
stage3_report=$stage3_report
stage0_build_log=$stage0_build_log
stage0_self_check_log=$stage0_self_check_log
stage1_build_log=$stage1_build_log
stage1_self_check_log=$stage1_self_check_log
stage2_build_log=$stage2_build_log
stage2_self_check_log=$stage2_self_check_log
stage3_build_log=$stage3_build_log
stage3_self_check_log=$stage3_self_check_log
stage2_contract_log=$stage2_contract_log
stage3_contract_log=$stage3_contract_log
fixed_point=stage2_stage3_contract_match
EOF

echo "v3 bootstrap bridge ok: $stage2"
