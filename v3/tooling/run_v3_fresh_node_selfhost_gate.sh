#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
default_compiler="$root/artifacts/v3_backend_driver/cheng"
compiler="${1:-$default_compiler}"
label="${2:-host}"
src="$root/v3/src/tests/program_selfhost_smoke.cheng"
out_dir="$root/artifacts/v3_fresh_node_gate/$label"
sync_prefix="$out_dir/program_selfhost_smoke.sync"
fresh_prefix="$out_dir/program_selfhost_smoke.fresh"
sync_log="$out_dir/world_sync.log"
fresh_log="$out_dir/fresh_node.log"

mkdir -p "$out_dir"
cd "$root"

if [ "$compiler" = "$default_compiler" ] && [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

if [ ! -x "$compiler" ]; then
  echo "v3 fresh-node selfhost gate: missing compiler: $compiler" >&2
  exit 1
fi

if [ ! -f "$src" ]; then
  echo "v3 fresh-node selfhost gate: missing source: $src" >&2
  exit 1
fi

echo "[v3 fresh-node selfhost gate] world-sync $label"
"$compiler" world-sync \
  "--root:$root/v3" \
  "--in:$src" \
  "--target:arm64-apple-darwin" \
  "--out:$sync_prefix" \
  "--channel:stable" >"$sync_log" 2>&1
cat "$sync_log"

for suffix in .world.txt .compiler.snapshot.txt .std.snapshot.txt .runtime.snapshot.txt .csg.txt .surface.txt .receipt.txt .lock.toml
do
  if [ ! -f "$sync_prefix$suffix" ]; then
    echo "v3 fresh-node selfhost gate: missing world artifact: $sync_prefix$suffix" >&2
    exit 1
  fi
done

echo "[v3 fresh-node selfhost gate] fresh-node-selfhost $label"
"$compiler" fresh-node-selfhost \
  "--root:$root/v3" \
  "--in:$src" \
  "--target:arm64-apple-darwin" \
  "--out:$fresh_prefix" \
  "--channel:stable" \
  "--baseline-csg:$sync_prefix.csg.txt" \
  "--baseline-surface:$sync_prefix.surface.txt" \
  "--baseline-receipt:$sync_prefix.receipt.txt" >"$fresh_log" 2>&1
cat "$fresh_log"

grep -Fxq 'equivalence_passed=1' "$fresh_log"
grep -Fxq 'publish_allowed=1' "$fresh_log"
grep -Fxq 'receipt_equivalent=1' "$fresh_log"
grep -Fxq 'receipt_cid_match=1' "$fresh_log"

if [ ! -f "$fresh_prefix.fresh.txt" ]; then
  echo "v3 fresh-node selfhost gate: missing fresh report: $fresh_prefix.fresh.txt" >&2
  exit 1
fi

grep -Fxq 'receipt_equivalent=1' "$fresh_prefix.fresh.txt"
grep -Fxq 'receipt_cid_match=1' "$fresh_prefix.fresh.txt"

echo "v3 fresh-node selfhost gate ok ($label)"
