#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out_dir="$root/artifacts/v3_stage23_libp2p"
stage2="$root/artifacts/v3_bootstrap/cheng.stage2"
stage3="$root/artifacts/v3_bootstrap/cheng.stage3"
mkdir -p "$out_dir"
cd "$root"

for compiler in "$stage2" "$stage3"
do
  if [ ! -x "$compiler" ]; then
    echo "v3 stage2/stage3 libp2p: missing compiler: $compiler" >&2
    exit 1
  fi
done

run_stage_smoke() {
  compiler="$1"
  stage_name="$2"
  smoke_name="$3"
  src="$root/v3/src/tests/$smoke_name.cheng"
  bin="$out_dir/$smoke_name.$stage_name"
  BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
    zsh "$root/v3/tooling/run_v3_single_smoke.sh" \
      "v3 stage2/stage3 libp2p" \
      "$stage_name $smoke_name" \
      "$compiler" \
      "$root/v3" \
      "$src" \
      "$bin"
}

run_twoproc_smoke() {
  compiler="$1"
  stage_name="$2"
  BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
    sh "$root/v3/tooling/run_v3_tcp_twoproc_smoke.sh" "$compiler" "$stage_name"
}

tests="
fixed256_curve25519_smoke
compiler_runtime_smoke
compiler_pipeline_stub_smoke
lowering_plan_smoke
compiler_equivalence_smoke
compiler_publish_smoke
bft_three_replica_smoke
primary_object_plan_smoke
object_native_link_plan_smoke
program_selfhost_smoke
libp2p_core_smoke
libp2p_multiaddr_parse_boundary_smoke
libp2p_host_smoke
libp2p_tailnet_policy_smoke
libp2p_tailnet_visibility_smoke
libp2p_tailnet_transport_smoke
libp2p_tailnet_derp_smoke
libp2p_resource_smoke
libp2p_scheduler_smoke
tailnet_control_core_smoke
tailnet_train_island_smoke
webrtc_signal_codec_smoke
webrtc_signal_session_smoke
webrtc_turn_fallback_smoke
libp2p_webrtc_sync_smoke
libp2p_tcp_smoke
libp2p_overlay_smoke
libp2p_protocols_smoke
chain_codec_binary_smoke
location_proof_smoke
anti_entropy_smoke
anti_entropy_signature_fields_smoke
lsmr_types_smoke
lsmr_locality_storage_smoke
lsmr_bagua_prefix_tree_smoke
overlay_contracts_smoke
pubsub_smoke
dag_mempool_smoke
plumtree_smoke
erasure_swarm_smoke
pin_plane_smoke
pin_scheduler_smoke
content_codec_smoke
content_runtime_smoke
content_stub_smoke
chain_node_tailnet_smoke
chain_node_libp2p_smoke
chain_node_zero_snapshot_replay_smoke
mobile_bridge_smoke
mobile_sdk_smoke
did_identity_smoke
"

for smoke_name in $tests
do
  run_stage_smoke "$stage2" stage2 "$smoke_name"
  if [ "$smoke_name" = "libp2p_tcp_smoke" ]; then
    run_twoproc_smoke "$stage2" stage2
  fi
  run_stage_smoke "$stage3" stage3 "$smoke_name"
  if [ "$smoke_name" = "libp2p_tcp_smoke" ]; then
    run_twoproc_smoke "$stage3" stage3
  fi
done

BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_chain_node_cli_smoke.sh" "$stage2" stage2
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_chain_node_cli_smoke.sh" "$stage3" stage3
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_chain_node_process_smoke.sh" "$stage2" stage2
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_chain_node_process_smoke.sh" "$stage3" stage3
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_chain_node_three_node_smoke.sh" "$stage2" stage2
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_chain_node_three_node_smoke.sh" "$stage3" stage3
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_tailnet_control_smoke.sh" "$stage2" stage2
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_tailnet_control_smoke.sh" "$stage3" stage3
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_fresh_node_selfhost_gate.sh" "$stage2" stage2
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_fresh_node_selfhost_gate.sh" "$stage3" stage3
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_migration_gate.sh" "$stage2" stage2
BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_migration_gate.sh" "$stage3" stage3
CHENG_V3_SMOKE_COMPILER="$stage3" BACKEND_INCREMENTAL=0 BACKEND_MULTI_MODULE_CACHE=0 \
  sh "$root/v3/tooling/run_v3_browser_webrtc_smokes.sh"

echo "v3 stage2/stage3 libp2p: ok"
