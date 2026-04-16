#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
out_dir="$root/artifacts/v3_bootstrap"
backend_driver="$root/artifacts/v3_backend_driver/cheng"
seed_source="$root/v3/bootstrap/cheng_v3_seed.c"
stage1_source="$root/v3/bootstrap/stage1_bootstrap.cheng"
stage3_path="$out_dir/cheng.stage3"
stage0_path="$out_dir/cheng.stage0"
seed_runner="$out_dir/cheng.bootstrap_bridge_runner"
bridge_driver="$out_dir/cheng.bootstrap_bridge"
seed_runner_log="$out_dir/cheng.bootstrap_bridge_runner.build.log"
bridge_driver_log="$out_dir/cheng.bootstrap_bridge.report.txt"
package_root="$root/v3"
compiler_entry="$root/v3/src/tooling/compiler_main.cheng"
compiler_runtime="$root/v3/src/tooling/compiler_runtime.cheng"
compiler_request="$root/v3/src/tooling/compiler_request.cheng"
gate_source="$root/v3/src/tooling/gate_main.cheng"
bootstrap_contracts="$root/v3/src/tooling/bootstrap_contracts.cheng"
cc_bin="${CC:-cc}"
target_triple="$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]')"

mkdir -p "$out_dir"

v3_bin_fresh() {
  bin="$1"
  shift
  [ -x "$bin" ] || return 1
  for src in "$@"; do
    if [ "$src" -nt "$bin" ]; then
      return 1
    fi
  done
  return 0
}

if v3_bin_fresh \
  "$backend_driver" \
  "$seed_source" \
  "$stage1_source" \
  "$compiler_entry" \
  "$compiler_runtime" \
  "$compiler_request" \
  "$gate_source" \
  "$bootstrap_contracts" && \
  "$backend_driver" help 2>/dev/null | grep -q "bootstrap-bridge"; then
  exec "$backend_driver" bootstrap-bridge "$@"
fi

if v3_bin_fresh \
  "$stage3_path" \
  "$seed_source" \
  "$stage1_source" \
  "$compiler_entry" \
  "$compiler_runtime" \
  "$compiler_request" \
  "$gate_source" \
  "$bootstrap_contracts"; then
  exec "$stage3_path" bootstrap-bridge "$@"
fi

if v3_bin_fresh \
  "$stage0_path" \
  "$seed_source" \
  "$stage1_source" \
  "$compiler_entry" \
  "$compiler_runtime" \
  "$compiler_request" \
  "$gate_source" \
  "$bootstrap_contracts"; then
  exec "$stage0_path" bootstrap-bridge "$@"
fi

if [ -x "$stage3_path" ] && \
   [ ! "$seed_source" -nt "$stage3_path" ] && \
   [ ! "$stage1_source" -nt "$stage3_path" ]; then
  case "$(uname -s)" in
    Darwin)
      target_triple="$(uname -m)-apple-darwin"
      ;;
    Linux)
      target_triple="$(uname -m)-unknown-linux-gnu"
      ;;
  esac
  if ! v3_bin_fresh \
    "$bridge_driver" \
    "$compiler_entry" \
    "$compiler_runtime" \
    "$compiler_request" \
    "$gate_source" \
    "$bootstrap_contracts"; then
    "$stage3_path" system-link-exec \
      --root:"$package_root" \
      --in:"$compiler_entry" \
      --emit:exe \
      --target:"$target_triple" \
      --out:"$bridge_driver" \
      --report-out:"$bridge_driver_log"
  fi
  exec "$bridge_driver" bootstrap-bridge "$@"
fi

if [ ! -f "$seed_source" ]; then
  echo "v3 bootstrap bridge: missing seed source: $seed_source" >&2
  exit 1
fi

if [ ! -x "$seed_runner" ] || [ "$seed_source" -nt "$seed_runner" ]; then
  if ! "$cc_bin" -std=c11 -O2 -Wall -Wextra -pedantic \
    "$seed_source" -o "$seed_runner" >"$seed_runner_log" 2>&1; then
    echo "v3 bootstrap bridge: temporary seed runner build failed log=$seed_runner_log" >&2
    tail -n 80 "$seed_runner_log" >&2 || true
    exit 1
  fi
fi

if [ ! -x "$seed_runner" ]; then
  echo "v3 bootstrap bridge: missing temporary seed runner: $seed_runner" >&2
  exit 1
fi

exec "$seed_runner" bootstrap-bridge "$@"
