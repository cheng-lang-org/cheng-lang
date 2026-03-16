#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_dod_soa] rg is required" >&2
  exit 2
fi

out_dir="artifacts/backend_dod_soa"
mkdir -p "$out_dir"
report_file="$out_dir/backend_dod_soa.report.txt"
snapshot_file="$out_dir/backend_dod_soa.snapshot.env"
missing_file="$out_dir/backend_dod_soa.missing_markers.txt"
: >"$missing_file"

check_marker() {
  file="$1"
  pattern="$2"
  name="$3"
  if ! rg -q "$pattern" "$file"; then
    printf '%s\t%s\t%s\n' "$file" "$name" "$pattern" >>"$missing_file"
  fi
}

check_marker "src/stage1/ast.cheng" 'nodeArena: Node\[\]' 'stage1_node_arena'
check_marker "src/stage1/ast.cheng" 'nodeArenaEnabled: bool = true' 'stage1_node_arena_enabled'
check_marker "src/backend/uir/uir_internal/uir_core_types.cheng" 'localIndex: int32' 'uir_local_index_int32'
check_marker "src/backend/uir/uir_internal/uir_core_types.cheng" 'slot: int32' 'uir_slot_index_int32'
check_marker "src/backend/uir/uir_internal/uir_core_types.cheng" 'frameOff: int32' 'uir_frame_offset_int32'
check_marker "src/backend/uir/uir_internal/uir_core_ssa.cheng" 'rpoIndex: int32\[\]' 'uir_ssa_rpo_index_int32'

status="ok"
if [ -s "$missing_file" ]; then
  status="drift"
fi

{
  echo "verify_backend_dod_soa report"
  echo "status=$status"
  echo "mode=source_contract"
  echo "missing_markers=$missing_file"
} >"$report_file"

{
  echo "backend_dod_soa_status=$status"
  echo "backend_dod_soa_mode=source_contract"
  echo "backend_dod_soa_missing_markers=$missing_file"
  echo "backend_dod_soa_report=$report_file"
} >"$snapshot_file"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_dod_soa] missing required markers; see $missing_file" >&2
  exit 1
fi

echo "verify_backend_dod_soa ok"
