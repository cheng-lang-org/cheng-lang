#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

if [ "${BACKEND_DRIVER:-}" != "" ]; then
  driver="${BACKEND_DRIVER}"
else
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi
if [ ! -x "$driver" ]; then
  echo "[verify_backend_metering_stream] backend driver not executable: $driver" 1>&2
  exit 1
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_metering_stream] rg is required" 1>&2
  exit 2
fi

hash_file() {
  file="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$file" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$file" | awk '{print $1}'
    return
  fi
  cksum "$file" | awk '{print $1}'
}

require_marker() {
  file="$1"
  pattern="$2"
  marker_name="$3"
  if ! rg -q "$pattern" "$file"; then
    echo "[verify_backend_metering_stream] missing marker ($marker_name) in $file" 1>&2
    exit 1
  fi
}

compile_metering_obj() {
  mode="$1"
  out_obj="$2"
  out_log="$3"

  set +e
  env \
    PLUGIN_ENABLE=1 \
    PLUGIN_PATHS= \
    METERING_PLUGIN=metering.default \
    STAGE1_STD_NO_POINTERS=0 \
    STAGE1_STD_NO_POINTERS_STRICT=0 \
    STAGE1_NO_POINTERS_NON_C_ABI=0 \
    STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
    STAGE1_SKIP_SEM="${STAGE1_SKIP_SEM:-0}" \
    STAGE1_SKIP_OWNERSHIP="${STAGE1_SKIP_OWNERSHIP:-1}" \
    BACKEND_PROFILE=1 \
    BACKEND_MULTI="$mode" \
    BACKEND_MULTI_FORCE="$mode" \
    BACKEND_JOBS="${BACKEND_JOBS:-4}" \
    BACKEND_ALLOW_NO_MAIN=1 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_TARGET="$target" \
    BACKEND_FRONTEND=stage1 \
    BACKEND_EMIT=obj \
    BACKEND_INPUT=src/decentralized/metering.cheng \
    BACKEND_OUTPUT="$out_obj" \
    "$driver" >"$out_log" 2>&1
  status="$?"
  set -e
  return "$status"
}

target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
out_dir="artifacts/backend_metering_stream"
mkdir -p "$out_dir"

serial_log="$out_dir/metering_stream.serial.$safe_target.log"
multi_log="$out_dir/metering_stream.multi.$safe_target.log"
serial_obj="$out_dir/metering_stream.serial.$safe_target.o"
multi_obj="$out_dir/metering_stream.multi.$safe_target.o"
report_file="$out_dir/backend_metering_stream.report.txt"
snapshot_file="$out_dir/backend_metering_stream.snapshot.env"
diff_file="$out_dir/backend_metering_stream.obj.diff.txt"

rm -f "$serial_log" "$multi_log" "$serial_obj" "$multi_obj" "$report_file" "$snapshot_file" "$diff_file"

# DEC-01: metering injection points must stay stable.
require_marker "src/decentralized/metering_sdk.cheng" 'template withMetering\(' 'metering_sdk_with_metering'
require_marker "src/decentralized/metering_sdk.cheng" 'template withIoMetering\(' 'metering_sdk_with_io_metering'
require_marker "src/decentralized/metering_sdk.cheng" 'template withMeteringIo\(' 'metering_sdk_with_metering_io'
require_marker "src/decentralized/metering_sdk.cheng" 'os.setIoMeterHook' 'metering_sdk_io_hook_install'
require_marker "src/decentralized/metering_sdk.cheng" 'appendComputeEventWithMode' 'metering_sdk_flush_ledger'
require_marker "src/backend/uir/uir_internal/uir_core_builder.cheng" 'getEnv "METERING_PLUGIN"' 'uir_metering_plugin_env'
require_marker "src/backend/uir/uir_internal/uir_core_builder.cheng" 'strHasPrefix\(path, "cheng/decentralized/"\)' 'uir_metering_plugin_scope'
require_marker "src/backend/uir/uir_internal/uir_core_builder.cheng" 'fn uirCoreApplyAstPluginHooks' 'uir_ast_plugin_hook'
require_marker "src/backend/uir/uir_internal/uir_core_builder.cheng" 'fn uirCoreApplyMirPluginHooks' 'uir_mir_plugin_hook'

# DEC-03: gas formula contract must remain explicit.
require_marker "src/decentralized/metering.cheng" 'let cpuCost: float64 = float64\(usage.cpuMs\) \* usage.priceCpuMs' 'gas_cpu_formula'
require_marker "src/decentralized/metering.cheng" 'let memCost: float64 = \(float64\(usage.memBytes\) / \(1024.0 \* 1024.0 \* 1024.0\)\) \* usage.priceMemGb' 'gas_mem_formula'
require_marker "src/decentralized/metering.cheng" 'let ioCost: float64 = \(float64\(usage.ioBytes\) / \(1024.0 \* 1024.0 \* 1024.0\)\) \* usage.priceIoGb' 'gas_io_formula'
require_marker "src/decentralized/metering.cheng" 'let gpuCost: float64 = float64\(usage.gpuMs\) \* usage.priceGpuMs' 'gas_gpu_formula'
require_marker "src/decentralized/metering.cheng" 'let gpuMemCost: float64 = \(float64\(usage.gpuMemBytes\) / \(1024.0 \* 1024.0 \* 1024.0\)\) \* usage.priceGpuMemGb' 'gas_gpu_mem_formula'
require_marker "src/decentralized/metering.cheng" 'cost.total = cpuCost \+ memCost \+ ioCost \+ gpuCost \+ gpuMemCost' 'gas_total_formula'
require_marker "src/decentralized/metering.cheng" 'cost.royalty = cost.total \* usage.royaltyRate' 'gas_royalty_formula'
require_marker "src/decentralized/metering.cheng" 'cost.treasury = cost.total \* usage.treasuryRate' 'gas_treasury_formula'
require_marker "src/decentralized/metering.cheng" 'cost.executor = cost.total - cost.royalty - cost.treasury' 'gas_executor_formula'

# DEC-02: streaming compile path must at least pass with multi mode.
set +e
compile_metering_obj 0 "$serial_obj" "$serial_log"
serial_status="$?"
compile_metering_obj 1 "$multi_obj" "$multi_log"
multi_status="$?"
set -e

if [ "$serial_status" -ne 0 ]; then
  echo "[verify_backend_metering_stream] serial compile failed (status=$serial_status): $serial_log" 1>&2
  sed -n '1,120p' "$serial_log" 1>&2 || true
  exit 1
fi
if [ "$multi_status" -ne 0 ]; then
  echo "[verify_backend_metering_stream] streaming compile failed (status=$multi_status): $multi_log" 1>&2
  sed -n '1,120p' "$multi_log" 1>&2 || true
  exit 1
fi
if [ ! -s "$serial_obj" ] || [ ! -s "$multi_obj" ]; then
  echo "[verify_backend_metering_stream] missing object output(s)" 1>&2
  exit 1
fi

if ! rg -q 'backend_profile[[:space:]]+build_module' "$serial_log"; then
  echo "[verify_backend_metering_stream] serial compile log missing backend_profile build_module" 1>&2
  exit 1
fi
if ! rg -q 'backend_profile[[:space:]]+build_module' "$multi_log"; then
  echo "[verify_backend_metering_stream] streaming compile log missing backend_profile build_module" 1>&2
  exit 1
fi

streaming_path_mode=""
if rg -q 'backend_profile[[:space:]]+multi\.plan' "$multi_log"; then
  streaming_path_mode="multi.plan"
elif rg -q 'backend_profile[[:space:]]+single\.emit_obj' "$multi_log"; then
  streaming_path_mode="single.emit_obj.fastpath"
else
  echo "[verify_backend_metering_stream] streaming compile log missing multi/single emit marker" 1>&2
  exit 1
fi

serial_sha="$(hash_file "$serial_obj")"
multi_sha="$(hash_file "$multi_obj")"
obj_identical="1"
if ! cmp -s "$serial_obj" "$multi_obj"; then
  obj_identical="0"
  {
    echo "serial_obj=$serial_obj"
    echo "multi_obj=$multi_obj"
    echo "serial_sha256=$serial_sha"
    echo "multi_sha256=$multi_sha"
  } >"$diff_file"
fi

if [ "$obj_identical" != "1" ]; then
  echo "[verify_backend_metering_stream] gas consistency drift: serial/multi object mismatch" 1>&2
  echo "  diff: $diff_file" 1>&2
  exit 1
fi

{
  echo "verify_backend_metering_stream report"
  echo "status=ok"
  echo "driver=$driver"
  echo "target=$target"
  echo "serial_obj=$serial_obj"
  echo "multi_obj=$multi_obj"
  echo "serial_obj_sha256=$serial_sha"
  echo "multi_obj_sha256=$multi_sha"
  echo "obj_identical=$obj_identical"
  echo "streaming_path_mode=$streaming_path_mode"
  echo "serial_log=$serial_log"
  echo "multi_log=$multi_log"
  echo "metering_injection_points=ok"
  echo "metering_gas_formula=ok"
} >"$report_file"

{
  echo "backend_metering_stream_target=$target"
  echo "backend_metering_stream_driver=$driver"
  echo "backend_metering_stream_serial_obj_sha256=$serial_sha"
  echo "backend_metering_stream_multi_obj_sha256=$multi_sha"
  echo "backend_metering_stream_obj_identical=$obj_identical"
  echo "backend_metering_stream_streaming_path_mode=$streaming_path_mode"
  echo "backend_metering_stream_report=$report_file"
} >"$snapshot_file"

echo "verify_backend_metering_stream ok"
