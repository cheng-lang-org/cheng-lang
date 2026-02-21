#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

scan_paths_csv="${BACKEND_NO_OBJ_SCAN_PATHS:-artifacts/backend_closedloop:artifacts/backend_profile_smoke:artifacts/backend_profile_schema:artifacts/backend_mem_contract:artifacts/backend_dod_contract:artifacts/backend_mem_image_core:artifacts/backend_mem_exe_emit:artifacts/backend_profile_baseline:artifacts/backend_linkerless_dev:artifacts/backend_hotpatch_meta:artifacts/backend_hotpatch_inplace:artifacts/backend_incr_patch_fastpath:artifacts/backend_mem_patch_regression:artifacts/backend_hotpatch:artifacts/backend_plugin_isolation:artifacts/backend_linker_abi_core:artifacts/backend_self_linker_elf:artifacts/backend_self_linker_coff:artifacts/backend_prod:dist/releases/current}"

out_dir="artifacts/backend_no_obj_artifacts"
report="$out_dir/backend_no_obj_artifacts.report.txt"
mkdir -p "$out_dir"

matches_file="$out_dir/backend_no_obj_artifacts.matches.txt"
: >"$matches_file"

old_ifs="$IFS"
IFS=':'
for p in $scan_paths_csv; do
  IFS="$old_ifs"
  [ "$p" != "" ] || continue
  if [ ! -e "$p" ]; then
    continue
  fi
  find "$p" \( -type f -o -type d \) \
    \( -name '*.o' -o -name '*.obj' -o -name '*.objs' -o -name '*.objs.lock' -o -name '*.tmp.linkobj' \) \
    -print >>"$matches_file"
done
IFS="$old_ifs"

if [ -s "$matches_file" ]; then
  LC_ALL=C sort -u "$matches_file" -o "$matches_file"
fi

count="0"
if [ -s "$matches_file" ]; then
  count="$(wc -l < "$matches_file" | tr -d ' ')"
fi

{
  echo "scan_paths=$scan_paths_csv"
  echo "obj_like_count=$count"
  if [ "$count" != "0" ]; then
    echo "obj_like_paths_file=$matches_file"
  fi
} >"$report"

if [ "$count" != "0" ]; then
  echo "[verify_backend_no_obj_artifacts] found obj-like artifacts in closure outputs:" >&2
  sed 's/^/  - /' "$matches_file" >&2
  echo "[verify_backend_no_obj_artifacts] report=$report" >&2
  exit 1
fi

echo "verify_backend_no_obj_artifacts ok"
