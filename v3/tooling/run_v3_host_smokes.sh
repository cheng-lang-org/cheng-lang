#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_hostc="$root/artifacts/v3_backend_driver/cheng"
hostc="${CHENG_V3_SMOKE_COMPILER:-$default_hostc}"
out_dir="$root/artifacts/v3_hostrun"
mkdir -p "$out_dir"
cd "$root"

if [ "${CHENG_V3_SMOKE_COMPILER:-}" = "" ] && [ ! -x "$hostc" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$hostc" ]; then
  echo "v3 host smokes: missing host compiler: $hostc" >&2
  exit 1
fi

run_host_smoke() {
  name="$1"
  src="$root/v3/src/tests/$name.cheng"
  bin="$out_dir/$name"
  compile_log="$out_dir/$name.compile.log"
  run_log="$out_dir/$name.run.log"
  if [ ! -f "$src" ]; then
    echo "v3 host smokes: missing source: $src" >&2
    exit 1
  fi
  rm -f "$bin" "$bin".* "$compile_log" "$run_log"
  echo "[v3 host smokes] compile $name"
  if ! DIAG_CONTEXT=1 "$hostc" system-link-exec \
    --root "$root/v3" \
    --in "$src" \
    --emit exe \
    --target arm64-apple-darwin \
    --out "$bin" >"$compile_log" 2>&1; then
    echo "v3 host smokes: compile failed: $name" >&2
    tail -n 80 "$compile_log" >&2 || true
    exit 1
  fi
  echo "[v3 host smokes] run $name"
  if ! "$bin" >"$run_log" 2>&1; then
    echo "v3 host smokes: run failed: $name" >&2
    tail -n 80 "$run_log" >&2 || true
    exit 1
  fi
  cat "$run_log"
}

run_host_twoproc_smoke() {
  sh "$root/v3/tooling/run_v3_tcp_twoproc_smoke.sh" "$hostc" host
}

tests="
ref10_ashr_smoke
fixed256_curve25519_smoke
fixedbytes32_seq_index_smoke
compiler_runtime_smoke
rwad_serial_state_machine_smoke
rwad_bft_state_machine_smoke
parser_path_smoke
compiler_pipeline_stub_smoke
lowering_plan_smoke
primary_object_plan_smoke
object_native_link_plan_smoke
ffi_handle_smoke
program_selfhost_smoke
csg_smoke
consensus_smoke
chain_node_smoke
chain_node_libp2p_smoke
bft_finalize_summary_smoke
bft_state_machine_smoke
overlay_contracts_smoke
pubsub_smoke
dag_mempool_smoke
plumtree_smoke
erasure_swarm_smoke
content_codec_smoke
content_runtime_smoke
native_initial_crypto_frame_smoke
native_client_hello_wire_smoke
quic_transport_loopback_smoke
libp2p_quic_tls_smoke
content_quic_smoke
content_stub_smoke
libp2p_protocols_smoke
location_proof_smoke
chain_codec_binary_smoke
anti_entropy_smoke
anti_entropy_signature_fields_smoke
lsmr_types_smoke
lsmr_locality_storage_smoke
lsmr_bagua_prefix_tree_smoke
udp_importc_smoke
"

run_tail_process_smoke=1
if [ "$#" -gt 0 ]; then
  tests="$*"
  run_tail_process_smoke=0
fi

for name in $tests
do
  run_host_smoke "$name"
  if [ "$name" = "chain_node_libp2p_smoke" ]; then
    run_host_twoproc_smoke
  fi
done

if [ "$run_tail_process_smoke" = "1" ]; then
  sh "$root/v3/tooling/run_v3_chain_node_process_smoke.sh" "$hostc" host
fi

echo "v3 host smokes: ok"
