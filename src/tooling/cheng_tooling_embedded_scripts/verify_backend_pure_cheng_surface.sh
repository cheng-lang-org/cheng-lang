#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "help" ]; then
  echo "Usage: sh src/tooling/cheng_tooling_embedded_scripts/verify_backend_pure_cheng_surface.sh"
  echo "Verifies pure-Cheng surface drift for real source only: legacy statement-style new, raw pointer/code-memory primitives, and default build-graph native-substrate references."
  exit 0
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_pure_cheng_surface] rg is required" >&2
  exit 2
fi

out_dir="$root/artifacts/backend_pure_cheng_surface"
rm -rf "$out_dir"
mkdir -p "$out_dir"

report="$out_dir/backend_pure_cheng_surface.report.txt"
snapshot="$out_dir/backend_pure_cheng_surface.snapshot.env"
legacy_new_file="$out_dir/backend_pure_cheng_surface.legacy_new.txt"
rawptr_file="$out_dir/backend_pure_cheng_surface.rawptr.txt"
c_files_file="$out_dir/backend_pure_cheng_surface.repo_c_files.txt"
manifest_refs_file="$out_dir/backend_pure_cheng_surface.manifest_refs.txt"
default_graph_source="$out_dir/backend_pure_cheng_surface.default_graph_source.cheng"

status="ok"
enforce="${BACKEND_PURE_CHENG_ENFORCE:-1}"

rg -n --glob '*.cheng' --glob '!cheng_sidecar_rewrite_*.cheng' \
  '(^[[:space:]]*new[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*$|(^|[^[:alnum:]_])new\([a-z_][A-Za-z0-9_]*\))' \
  src/stage1 src/std src/backend src/web tests \
  | rg -v '/compile_fail_new_local_var_ref_surface\.cheng:' >"$legacy_new_file" || true

rg -n --glob '*.cheng' --glob '!cheng_sidecar_rewrite_*.cheng' \
  '(void\*|[A-Za-z_][A-Za-z0-9_]*\*|ptr\[[^]]*\]|ptr_add\(|load_ptr\(|store_ptr\(|copyMem\(|setMem\(|zeroMem\(|alloc\(|dealloc\()' \
  src/stage1 src/std src/backend src/web \
  | rg -v 'rawPointerForbidMessage\(' >"$rawptr_file" || true

awk '/^fn tooling_cmdBackendProdClosureFromArgs\(/ { f=1 } f && /^fn [A-Za-z_][A-Za-z0-9_]*\(/ { if (seen) exit } f { print; seen=1 }' \
  src/tooling/cheng_tooling.cheng >"$default_graph_source"

rg -o 'tooling_cmdBackendProdRunGate\([^\n]*"(backend\.[A-Za-z0-9_\.]+)"' "$default_graph_source" \
  | sed -E 's/.*"(backend\.[^"]+)"/\1/' | LC_ALL=C sort -u >"$manifest_refs_file" || true

tmp_manifest_refs="${manifest_refs_file}.tmp"
: >"$tmp_manifest_refs"
: >"$c_files_file"
while IFS= read -r gate; do
  case "$gate" in
    backend.runtime_abi)
      printf '%s\n' \
        src/runtime/native/system_helpers.h \
        src/runtime/native/system_helpers.c \
        src/runtime/native/system_helpers_float_bits.c >>"$c_files_file"
      printf '%s\n' "$gate" >>"$tmp_manifest_refs"
      ;;
    backend.sidecar_cheng_fresh|backend.sidecar_cheng_seed)
      printf '%s\n' \
        src/backend/tooling/backend_driver_sidecar_outer_main.c \
        src/runtime/native/system_helpers.c \
        src/backend/tooling/backend_driver_uir_sidecar_bundle.c \
        src/backend/tooling/backend_driver_uir_sidecar_runtime_compat.c >>"$c_files_file"
      printf '%s\n' "$gate" >>"$tmp_manifest_refs"
      ;;
    backend.ffi_handle_sandbox|backend.ffi_borrow_bridge)
      printf '%s\n' \
        src/runtime/native/system_helpers.h \
        src/runtime/native/system_helpers.c >>"$c_files_file"
      printf '%s\n' "$gate" >>"$tmp_manifest_refs"
      ;;
  esac
done <"$manifest_refs_file"
mv "$tmp_manifest_refs" "$manifest_refs_file"
LC_ALL=C sort -u "$manifest_refs_file" -o "$manifest_refs_file"
LC_ALL=C sort -u "$c_files_file" -o "$c_files_file"

legacy_new_count="$(wc -l <"$legacy_new_file" | tr -d ' ')"
rawptr_count="$(wc -l <"$rawptr_file" | tr -d ' ')"
repo_c_file_count="$(wc -l <"$c_files_file" | tr -d ' ')"
manifest_ref_count="$(wc -l <"$manifest_refs_file" | tr -d ' ')"

if [ "$legacy_new_count" != "0" ] || [ "$rawptr_count" != "0" ] || [ "$repo_c_file_count" != "0" ] || [ "$manifest_ref_count" != "0" ]; then
  status="drift"
fi

{
  echo "verify_backend_pure_cheng_surface report"
  echo "manifest_scope=default_build_graph"
  echo "status=$status"
  echo "enforce=$enforce"
  echo "legacy_new_count=$legacy_new_count"
  echo "rawptr_count=$rawptr_count"
  echo "repo_c_file_count=$repo_c_file_count"
  echo "manifest_ref_count=$manifest_ref_count"
  echo "legacy_new_file=$legacy_new_file"
  echo "rawptr_file=$rawptr_file"
  echo "repo_c_file=$c_files_file"
  echo "manifest_refs_file=$manifest_refs_file"
} >"$report"

{
  echo "backend_pure_cheng_surface_status=$status"
  echo "backend_pure_cheng_surface_enforce=$enforce"
  echo "backend_pure_cheng_surface_report=$report"
  echo "backend_pure_cheng_surface_legacy_new_count=$legacy_new_count"
  echo "backend_pure_cheng_surface_rawptr_count=$rawptr_count"
  echo "backend_pure_cheng_surface_repo_c_file_count=$repo_c_file_count"
  echo "backend_pure_cheng_surface_manifest_ref_count=$manifest_ref_count"
} >"$snapshot"

if [ "$status" != "ok" ] && [ "$enforce" = "1" ]; then
  echo "[verify_backend_pure_cheng_surface] pure-Cheng surface drift detected" >&2
  echo "  report: $report" >&2
  sed -n '1,120p' "$legacy_new_file" >&2 || true
  sed -n '1,120p' "$rawptr_file" >&2 || true
  sed -n '1,120p' "$c_files_file" >&2 || true
  sed -n '1,120p' "$manifest_refs_file" >&2 || true
  exit 1
fi

echo "verify_backend_pure_cheng_surface ok"
