#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
bridge_env="$root/artifacts/v3_bootstrap/bootstrap.env"

v3_usage() {
  cat <<EOF
cheng_v3
usage:
  cheng_v3.sh scan-hotpath
  cheng_v3.sh c-ref
  cheng_v3.sh compare-bench <baseline> <candidate>
  cheng_v3.sh bootstrap-bridge
  cheng_v3.sh build-backend-driver
  cheng_v3.sh host-bridge-audit
  cheng_v3.sh status
  cheng_v3.sh slice-gate
  cheng_v3.sh run-smokes
  cheng_v3.sh debug-report [seed-flags...]
  cheng_v3.sh print-symbols [seed-flags...]
  cheng_v3.sh print-line-map [seed-flags...]
  cheng_v3.sh print-object --object:<path> [--report-out:<path>]
  cheng_v3.sh print-elf --object:<path> [--report-out:<path>]
  cheng_v3.sh print-asm [seed-flags...]
  cheng_v3.sh profile-run [seed-flags...]
  cheng_v3.sh profile-report --in:<raw-report> [--out:<final-report>]
  cheng_v3.sh crash-report --in:<raw-log> [--out:<final-report>]
  cheng_v3.sh verify-orphan-guard
  cheng_v3.sh verify-debug-tools
  cheng_v3.sh verify-debug-runtime
  cheng_v3.sh verify-debug-profile
  cheng_v3.sh verify-windows-builtin
  cheng_v3.sh verify-riscv64-builtin
  cheng_v3.sh run-cross-target-smokes
  cheng_v3.sh run-host-smokes [smoke_name ...]
  cheng_v3.sh run-stage23-libp2p-smokes [smoke_name ...]
  cheng_v3.sh build-zero-exit
  cheng_v3.sh build-panic-trace
  cheng_v3.sh build-bounds-trace
  cheng_v3.sh build-signal-trace
  cheng_v3.sh build-call-chain
  cheng_v3.sh build-ffi-handle
  cheng_v3.sh build-bft-state-machine
  cheng_v3.sh deploy-bft-validator-three-node
  cheng_v3.sh status-bft-validator-three-node
  cheng_v3.sh stop-bft-validator-three-node
  cheng_v3.sh bench-bft-validator-three-node [--tx-count:<n>] [--transfer-amount:<n>] [--max-txs-per-block:<n>]
  cheng_v3.sh build-browser-host-wasm [out.wasm]
  cheng_v3.sh build-rwad-bft-state-machine
  cheng_v3.sh build-program-selfhost
  cheng_v3.sh build-chain-node
  cheng_v3.sh build-linux-nolibc-exe
  cheng_v3.sh build-chain-node-linux
  cheng_v3.sh build-rwad-bft-linux
  cheng_v3.sh run-linux-object-smokes
  cheng_v3.sh run-tcp-twoproc-smoke
  cheng_v3.sh run-udp-importc-smoke
  cheng_v3.sh run-wasm-smokes
  cheng_v3.sh run-browser-host-wasm-smoke
  cheng_v3.sh run-chain-node-cli-smoke
  cheng_v3.sh run-chain-node-process-smoke
  cheng_v3.sh run-chain-node-three-node-smoke
  cheng_v3.sh run-tailnet-control-smoke
  cheng_v3.sh run-v2-selfhost-gate
  cheng_v3.sh run-fresh-node-selfhost-gate
  cheng_v3.sh run-migration-gate
  cheng_v3.sh run-browser-webrtc-smokes
  cheng_v3.sh r2c-react-v3 <subcommand> [args]
  cheng_v3.sh r2c-react-v3-fresh-clean-gate [--repo <path>]
  cheng_v3.sh print-bootstrap
  cheng_v3.sh print-build-plan
  cheng_v3.sh emit-csg [seed-flags...]
  cheng_v3.sh migrate-csg [seed-flags...]
  cheng_v3.sh verify-world [seed-flags...]
  cheng_v3.sh world-sync [seed-flags...]
  cheng_v3.sh prove-equivalence [seed-flags...]
  cheng_v3.sh prove-migration [seed-flags...]
  cheng_v3.sh publish-world [seed-flags...]
  cheng_v3.sh fresh-node-selfhost [seed-flags...]
  cheng_v3.sh selfhost-build [seed-flags...]
  cheng_v3.sh system-link-exec [seed-flags...]
EOF
}

v3_ensure_bridge() {
  sh "$root/v3/tooling/bootstrap_bridge_v3.sh" >/dev/null
  if [ ! -f "$bridge_env" ]; then
    echo "v3 tooling: missing bootstrap env: $bridge_env" >&2
    exit 1
  fi
}

v3_print_bootstrap() {
  v3_ensure_bridge
  . "$bridge_env"
  if [ -f "${V3_BOOTSTRAP_SNAPSHOT:-}" ]; then
    cat "$V3_BOOTSTRAP_SNAPSHOT"
    exit 0
  fi
  "$V3_BOOTSTRAP_STAGE2" print-contract --in:"$V3_BOOTSTRAP_STAGE1_SOURCE"
}

v3_ensure_backend_driver() {
  sh "$root/v3/tooling/build_backend_driver_v3.sh" >/dev/null
  if [ ! -x "$root/artifacts/v3_backend_driver/cheng" ]; then
    echo "v3 tooling: missing backend driver: $root/artifacts/v3_backend_driver/cheng" >&2
    exit 1
  fi
}

v3_is_backend_driver_cmd() {
  case "$1" in
    build-backend-driver|host-bridge-audit|status|print-build-plan|emit-csg|migrate-csg|verify-world|world-sync|prove-equivalence|prove-migration|publish-world|fresh-node-selfhost|selfhost-build|system-link-exec)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

v3_is_contract_cmd() {
  case "$1" in
    debug-report|print-symbols|print-line-map|print-asm|profile-run)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

v3_is_plain_stage3_cmd() {
  case "$1" in
    scan-hotpath|c-ref|compare-bench|slice-gate|run-smokes|print-object|print-elf|profile-report|crash-report|verify-orphan-guard|verify-debug-tools|verify-debug-runtime|verify-debug-profile|verify-windows-builtin|verify-riscv64-builtin|run-cross-target-smokes|run-host-smokes|run-stage23-libp2p-smokes|build-zero-exit|build-panic-trace|build-bounds-trace|build-signal-trace|build-call-chain|build-ffi-handle|build-bft-state-machine|deploy-bft-validator-three-node|status-bft-validator-three-node|stop-bft-validator-three-node|bench-bft-validator-three-node|build-browser-host-wasm|build-rwad-bft-state-machine|build-program-selfhost|build-chain-node|build-linux-nolibc-exe|build-chain-node-linux|build-rwad-bft-linux|run-linux-object-smokes|run-tcp-twoproc-smoke|run-udp-importc-smoke|run-wasm-smokes|run-browser-host-wasm-smoke|run-chain-node-cli-smoke|run-chain-node-process-smoke|run-chain-node-three-node-smoke|run-tailnet-control-smoke|run-v2-selfhost-gate|run-fresh-node-selfhost-gate|run-migration-gate|run-browser-webrtc-smokes|emit-csg|migrate-csg|verify-world|world-sync|prove-equivalence|prove-migration|publish-world|fresh-node-selfhost|selfhost-build|system-link-exec)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

v3_stage3_with_contract() {
  subcmd="$1"
  shift
  v3_ensure_bridge
  . "$bridge_env"
  exec "$V3_BOOTSTRAP_STAGE3" "$subcmd" --contract-in:"$V3_BOOTSTRAP_STAGE1_SOURCE" "$@"
}

v3_stage3_plain() {
  v3_ensure_bridge
  . "$bridge_env"
  exec "$V3_BOOTSTRAP_STAGE3" "$@"
}

v3_backend_driver_plain() {
  v3_ensure_backend_driver
  exec "$root/artifacts/v3_backend_driver/cheng" "$@"
}

cmd="${1:-help}"
shift || true

case "$cmd" in
  help|-h|--help)
    v3_usage
    ;;
  scan-hotpath)
    v3_stage3_plain scan-hotpath "$@"
    ;;
  c-ref)
    v3_stage3_plain c-ref "$@"
    ;;
  compare-bench)
    v3_stage3_plain compare-bench "$@"
    ;;
  bootstrap-bridge)
    exec sh "$root/v3/tooling/bootstrap_bridge_v3.sh" "$@"
    ;;
  r2c-react-v3)
    exec "$root/v3/experimental/r2c-react-v3/r2c-react-v3" "$@"
    ;;
  r2c-react-v3-fresh-clean-gate)
    exec sh "$root/v3/tooling/r2c_react_v3_fresh_clean_gate.sh" "$@"
    ;;
  print-bootstrap)
    v3_print_bootstrap
    ;;
  *)
    if v3_is_backend_driver_cmd "$cmd"; then
      v3_backend_driver_plain "$cmd" "$@"
    fi
    if v3_is_contract_cmd "$cmd"; then
      v3_stage3_with_contract "$cmd" "$@"
    fi
    if v3_is_plain_stage3_cmd "$cmd"; then
      v3_stage3_plain "$cmd" "$@"
    fi
    echo "v3 tooling: unknown command: $cmd" >&2
    v3_usage >&2
    exit 2
    ;;
esac
