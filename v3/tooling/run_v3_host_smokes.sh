#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
hostc="${CHENG_V3_SMOKE_COMPILER:-$root/v2/artifacts/bootstrap/cheng_v2c}"
out_dir="$root/artifacts/v3_hostrun"
mkdir -p "$out_dir"
cd "$root"

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

tests="
fixed_surface_smoke
compiler_runtime_smoke
parser_path_smoke
compiler_pipeline_stub_smoke
lowering_plan_smoke
primary_object_plan_smoke
object_native_link_plan_smoke
program_selfhost_smoke
csg_smoke
consensus_smoke
chain_node_smoke
pubsub_smoke
location_proof_smoke
chain_codec_binary_smoke
anti_entropy_smoke
lsmr_types_smoke
lsmr_locality_storage_smoke
lsmr_bagua_prefix_tree_smoke
"

for name in $tests
do
  run_host_smoke "$name"
done

echo "v3 host smokes: ok"
