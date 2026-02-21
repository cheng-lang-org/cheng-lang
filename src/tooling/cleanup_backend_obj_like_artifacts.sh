#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

scan_paths_csv="${BACKEND_NO_OBJ_SCAN_PATHS:-artifacts/backend_closedloop:artifacts/backend_profile_smoke:artifacts/backend_profile_schema:artifacts/backend_mem_contract:artifacts/backend_dod_contract:artifacts/backend_mem_image_core:artifacts/backend_mem_exe_emit:artifacts/backend_profile_baseline:artifacts/backend_linkerless_dev:artifacts/backend_hotpatch_meta:artifacts/backend_hotpatch_inplace:artifacts/backend_incr_patch_fastpath:artifacts/backend_mem_patch_regression:artifacts/backend_hotpatch:artifacts/backend_plugin_isolation:artifacts/backend_linker_abi_core:artifacts/backend_self_linker_elf:artifacts/backend_self_linker_coff:artifacts/backend_prod:dist/releases/current}"

out_dir="artifacts/backend_no_obj_artifacts"
mkdir -p "$out_dir"
deleted_file="$out_dir/backend_no_obj_artifacts.deleted.txt"
: >"$deleted_file"

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
    -print >>"$deleted_file"
done
IFS="$old_ifs"

if [ -s "$deleted_file" ]; then
  LC_ALL=C sort -u "$deleted_file" -o "$deleted_file"
  while IFS= read -r path; do
    [ "$path" != "" ] || continue
    rm -rf "$path" 2>/dev/null || true
  done <"$deleted_file"
fi

deleted_count="0"
if [ -s "$deleted_file" ]; then
  deleted_count="$(wc -l < "$deleted_file" | tr -d ' ')"
fi

echo "cleanup_backend_obj_like_artifacts ok (deleted=$deleted_count)"
