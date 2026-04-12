#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out_dir="$root/artifacts/v3_gate"
bridge_env="$root/artifacts/v3_bootstrap/bootstrap.env"
mkdir -p "$out_dir"

echo "[v3 gate] hot-path forbidden scan"
"$root/v3/tooling/scan_forbidden_hotpath.sh"

echo "[v3 gate] same-machine C baseline"
make -C "$root/v3/bench/c_ref" clean run | tee "$out_dir/c_ref_latest.txt"
if [ -f "$root/v3/bench/c_ref/baseline_arm64_apple_darwin.txt" ]; then
  echo "[v3 gate] frozen-vs-latest C baseline"
  "$root/v3/tooling/compare_bench.sh" \
    "$root/v3/bench/c_ref/baseline_arm64_apple_darwin.txt" \
    "$out_dir/c_ref_latest.txt"
fi

echo "[v3 gate] bootstrap bridge"
if ! sh "$root/v3/tooling/bootstrap_bridge_v3.sh" \
  >"$out_dir/bootstrap_bridge_v3.log" 2>&1; then
  echo "v3 gate: bootstrap_bridge_v3 failed" >&2
  tail -n 80 "$out_dir/bootstrap_bridge_v3.log" >&2 || true
  exit 1
fi

if [ ! -f "$bridge_env" ]; then
  echo "v3 gate: missing bootstrap env: $bridge_env" >&2
  exit 1
fi

. "$bridge_env"

echo "[v3 gate] canonical bootstrap compiler"
if ! sh "$root/v3/tooling/build_backend_driver_v3.sh" \
  >"$out_dir/build_backend_driver_v3.log" 2>&1; then
  echo "v3 gate: build_backend_driver_v3 failed" >&2
  tail -n 80 "$out_dir/build_backend_driver_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] host fixed256 sha256 smoke"
if ! sh "$root/v3/tooling/run_v3_host_smokes.sh" fixed256_sha256_smoke \
  >"$out_dir/run_v3_host_smokes.fixed256_sha256.log" 2>&1; then
  echo "v3 gate: fixed256_sha256_smoke failed" >&2
  tail -n 80 "$out_dir/run_v3_host_smokes.fixed256_sha256.log" >&2 || true
  exit 1
fi

echo "[v3 gate] host default-init literals smoke"
if ! sh "$root/v3/tooling/run_v3_host_smokes.sh" default_init_literals_smoke \
  >"$out_dir/run_v3_host_smokes.default_init_literals.log" 2>&1; then
  echo "v3 gate: default_init_literals_smoke failed" >&2
  tail -n 80 "$out_dir/run_v3_host_smokes.default_init_literals.log" >&2 || true
  exit 1
fi

echo "[v3 gate] stage2/stage3 libp2p smokes"
if ! sh "$root/v3/tooling/run_v3_stage23_libp2p_smokes.sh" \
  >"$out_dir/run_v3_stage23_libp2p_smokes.log" 2>&1; then
  echo "v3 gate: run_v3_stage23_libp2p_smokes failed" >&2
  tail -n 80 "$out_dir/run_v3_stage23_libp2p_smokes.log" >&2 || true
  exit 1
fi

echo "[v3 gate] v3 host smokes"
if ! sh "$root/v3/tooling/run_v3_host_smokes.sh" \
  >"$out_dir/run_v3_host_smokes.log" 2>&1; then
  echo "v3 gate: run_v3_host_smokes failed" >&2
  tail -n 80 "$out_dir/run_v3_host_smokes.log" >&2 || true
  exit 1
fi

echo "[v3 gate] zero-exit ordinary compile"
if ! sh "$root/v3/tooling/build_zero_exit_v3.sh" \
  >"$out_dir/build_zero_exit_v3.log" 2>&1; then
  echo "v3 gate: zero-exit ordinary compile 未接通" >&2
  tail -n 80 "$out_dir/build_zero_exit_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] panic-trace ordinary compile"
if ! sh "$root/v3/tooling/build_panic_trace_v3.sh" \
  >"$out_dir/build_panic_trace_v3.log" 2>&1; then
  echo "v3 gate: panic-trace ordinary compile 未接通" >&2
  tail -n 80 "$out_dir/build_panic_trace_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] bounds-trace ordinary compile"
if ! sh "$root/v3/tooling/build_bounds_trace_v3.sh" \
  >"$out_dir/build_bounds_trace_v3.log" 2>&1; then
  echo "v3 gate: bounds-trace ordinary compile 未接通" >&2
  tail -n 80 "$out_dir/build_bounds_trace_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] signal-trace ordinary compile"
if ! sh "$root/v3/tooling/build_signal_trace_v3.sh" \
  >"$out_dir/build_signal_trace_v3.log" 2>&1; then
  echo "v3 gate: signal-trace ordinary compile 未接通" >&2
  tail -n 80 "$out_dir/build_signal_trace_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] call-chain ordinary compile"
if ! sh "$root/v3/tooling/build_call_chain_v3.sh" \
  >"$out_dir/build_call_chain_v3.log" 2>&1; then
  echo "v3 gate: call-chain ordinary compile 未接通" >&2
  tail -n 80 "$out_dir/build_call_chain_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] ffi-handle ordinary compile"
if ! sh "$root/v3/tooling/build_ffi_handle_v3.sh" \
  >"$out_dir/build_ffi_handle_v3.log" 2>&1; then
  echo "v3 gate: ffi-handle ordinary compile 未接通" >&2
  tail -n 80 "$out_dir/build_ffi_handle_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] program-selfhost"
if ! sh "$root/v3/tooling/build_program_selfhost_v3.sh" \
  >"$out_dir/build_program_selfhost_v3.log" 2>&1; then
  echo "v3 gate: program-selfhost 未接通" >&2
  tail -n 80 "$out_dir/build_program_selfhost_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] chain_node"
if ! sh "$root/v3/tooling/build_chain_node_v3.sh" \
  >"$out_dir/build_chain_node_v3.log" 2>&1; then
  echo "v3 gate: chain_node 未接通" >&2
  tail -n 80 "$out_dir/build_chain_node_v3.log" >&2 || true
  exit 1
fi

echo "[v3 gate] bootstrap subset self-checks"
for bin in \
  "$V3_BOOTSTRAP_STAGE0" \
  "$V3_BOOTSTRAP_STAGE1" \
  "$V3_BOOTSTRAP_STAGE2" \
  "$V3_BOOTSTRAP_STAGE3" \
  "$root/artifacts/v3_backend_driver/cheng"
do
  name="$(basename "$bin")"
  log="$out_dir/$name.self-check.log"
  if ! "$bin" self-check --in:"$V3_BOOTSTRAP_STAGE1_SOURCE" >"$log" 2>&1; then
    echo "v3 gate: self-check failed: $name" >&2
    tail -n 80 "$log" >&2 || true
    exit 1
  fi
  cat "$log"
done

echo "[v3 gate] contract print equivalence"
if ! "$V3_BOOTSTRAP_STAGE2" print-contract --in:"$V3_BOOTSTRAP_STAGE1_SOURCE" \
  >"$out_dir/cheng.stage2.contract.txt" 2>"$out_dir/cheng.stage2.contract.stderr.log"; then
  echo "v3 gate: stage2 print-contract failed" >&2
  tail -n 80 "$out_dir/cheng.stage2.contract.stderr.log" >&2 || true
  exit 1
fi
if ! "$V3_BOOTSTRAP_STAGE3" print-contract --in:"$V3_BOOTSTRAP_STAGE1_SOURCE" \
  >"$out_dir/cheng.stage3.contract.txt" 2>"$out_dir/cheng.stage3.contract.stderr.log"; then
  echo "v3 gate: stage3 print-contract failed" >&2
  tail -n 80 "$out_dir/cheng.stage3.contract.stderr.log" >&2 || true
  exit 1
fi
if ! cmp -s "$out_dir/cheng.stage2.contract.txt" "$out_dir/cheng.stage3.contract.txt"; then
  echo "v3 gate: stage2/stage3 contract text mismatch" >&2
  exit 1
fi

echo "[v3 gate] ok"
