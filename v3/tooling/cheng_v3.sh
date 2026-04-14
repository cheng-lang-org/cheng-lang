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
  cheng_v3.sh slice-gate
  cheng_v3.sh run-smokes
  cheng_v3.sh debug-report [seed-flags...]
  cheng_v3.sh print-symbols [seed-flags...]
  cheng_v3.sh print-line-map [seed-flags...]
  cheng_v3.sh print-elf --object:<path> [--report-out:<path>]
  cheng_v3.sh verify-debug-tools
  cheng_v3.sh verify-windows-builtin
  cheng_v3.sh verify-riscv64-builtin
  cheng_v3.sh run-cross-target-smokes
  cheng_v3.sh print-bootstrap
  cheng_v3.sh print-build-plan
EOF
}

v3_ensure_bridge() {
  if [ ! -f "$bridge_env" ]; then
    sh "$root/v3/tooling/bootstrap_bridge_v3.sh"
  fi
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

v3_print_build_plan() {
  if [ ! -f "$root/artifacts/v3_backend_driver/build_backend_driver_v3.report.txt" ]; then
    sh "$root/v3/tooling/build_backend_driver_v3.sh"
  fi
  cat "$root/artifacts/v3_backend_driver/build_backend_driver_v3.report.txt"
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

v3_run_smokes() {
  v3_ensure_bridge
  . "$bridge_env"
  for bin in \
    "$V3_BOOTSTRAP_STAGE0" \
    "$V3_BOOTSTRAP_STAGE1" \
    "$V3_BOOTSTRAP_STAGE2" \
    "$V3_BOOTSTRAP_STAGE3"
  do
    "$bin" self-check --in:"$V3_BOOTSTRAP_STAGE1_SOURCE"
  done
}

cmd="${1:-help}"
shift || true

case "$cmd" in
  help|-h|--help)
    v3_usage
    ;;
  scan-hotpath)
    exec "$root/v3/tooling/scan_forbidden_hotpath.sh" "$@"
    ;;
  c-ref)
    exec make -C "$root/v3/bench/c_ref" clean run
    ;;
  compare-bench)
    exec "$root/v3/tooling/compare_bench.sh" "$@"
    ;;
  bootstrap-bridge)
    exec sh "$root/v3/tooling/bootstrap_bridge_v3.sh" "$@"
    ;;
  build-backend-driver)
    exec sh "$root/v3/tooling/build_backend_driver_v3.sh" "$@"
    ;;
  slice-gate)
    exec sh "$root/v3/tooling/run_slice_gate.sh" "$@"
    ;;
  run-smokes)
    v3_run_smokes
    ;;
  debug-report)
    v3_stage3_with_contract debug-report "$@"
    ;;
  print-symbols)
    v3_stage3_with_contract print-symbols "$@"
    ;;
  print-line-map)
    v3_stage3_with_contract print-line-map "$@"
    ;;
  print-elf)
    v3_stage3_plain print-elf "$@"
    ;;
  verify-debug-tools)
    exec sh "$root/v3/tooling/verify_debug_tools_v3.sh" "$@"
    ;;
  verify-windows-builtin)
    exec sh "$root/v3/tooling/verify_windows_builtin_linker_v3.sh" "$@"
    ;;
  verify-riscv64-builtin)
    exec sh "$root/v3/tooling/verify_riscv64_builtin_linker_v3.sh" "$@"
    ;;
  run-cross-target-smokes)
    exec sh "$root/v3/tooling/run_v3_windows_riscv_builtin_smokes.sh" "$@"
    ;;
  print-bootstrap)
    v3_print_bootstrap
    ;;
  print-build-plan)
    v3_print_build_plan
    ;;
  *)
    echo "v3 tooling: unknown command: $cmd" >&2
    v3_usage >&2
    exit 2
    ;;
esac
