#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_rawptr_surface_forbid] rg is required" 1>&2
  exit 2
fi

out_dir="artifacts/backend_rawptr_surface_forbid"
report="$out_dir/backend_rawptr_surface_forbid.report.txt"
snapshot="$out_dir/backend_rawptr_surface_forbid.snapshot.env"
mkdir -p "$out_dir"

status="ok"
hits_file="$out_dir/backend_rawptr_surface_forbid.hits.txt"
miss_file="$out_dir/backend_rawptr_surface_forbid.miss.txt"
: >"$hits_file"
: >"$miss_file"

require_hit() {
  file="$1"
  pattern="$2"
  label="$3"
  if rg -nF -- "$pattern" "$file" > /dev/null 2>&1; then
    printf '%s\t%s\t%s\n' "hit" "$label" "$file" >>"$hits_file"
    return 0
  fi
  printf '%s\t%s\t%s\t%s\n' "miss" "$label" "$file" "$pattern" >>"$miss_file"
  status="drift"
  return 1
}

require_absent() {
  file="$1"
  pattern="$2"
  label="$3"
  if rg -nF -- "$pattern" "$file" > /dev/null 2>&1; then
    printf '%s\t%s\t%s\t%s\n' "unexpected" "$label" "$file" "$pattern" >>"$miss_file"
    status="drift"
    return 1
  fi
  printf '%s\t%s\t%s\n' "absent" "$label" "$file" >>"$hits_file"
  return 0
}

diag_file="src/stage1/diagnostics.cheng"
parser_file="src/stage1/parser.cheng"
sem_file="src/stage1/semantics.cheng"

require_hit "$diag_file" "fn rawPointerForbidMessage(base: str): str =" "diagnostics.rawptr_message_fn"
require_hit "$diag_file" "slice/tuple/handle/borrow" "diagnostics.rawptr_replacements"

require_absent "$parser_file" "pointer is removed; use void*" "parser.remove_old_voidptr_suggestion"
require_absent "$parser_file" "Empty subscript dereference is not supported; use *p" "parser.remove_old_deref_suggestion"
require_hit "$parser_file" "rawPointerForbidMessage(\"pointer alias is removed and bare void* is forbidden\")" "parser.pointer_alias_forbid"
require_hit "$parser_file" "rawPointerForbidMessage(\"var is not allowed on pointer types\")" "parser.var_pointer_forbid"
require_hit "$parser_file" "rawPointerForbidMessage(\"Empty subscript dereference is not supported in no-pointer mode\")" "parser.empty_subscript_forbid"
require_hit "$parser_file" "rawPointerForbidMessage(\"Pointer member access is forbidden in no-pointer mode\")" "parser.pointer_member_forbid"

require_hit "$sem_file" "fn semIsVoidPtrTypeNode(n: Node): bool =" "semantics.voidptr_detect_fn"
require_hit "$sem_file" "rawPointerForbidMessage(\"no-pointer policy: bare void* surface is forbidden outside C ABI modules\")" "semantics.voidptr_forbid_diag"
require_hit "$sem_file" "rawPointerForbidMessage(\"no-pointer policy: pointer types are forbidden outside C ABI modules\")" "semantics.pointer_type_forbid_diag"
require_hit "$sem_file" "rawPointerForbidMessage(\"no-pointer policy: pointer dereference is forbidden outside C ABI modules\")" "semantics.pointer_deref_forbid_diag"
require_hit "$sem_file" "rawPointerForbidMessage(\"no-pointer policy: pointer operation is forbidden outside C ABI modules: \" + callName)" "semantics.pointer_op_forbid_diag"

runtime_probe_status="skip"

{
  echo "verify_backend_rawptr_surface_forbid report"
  echo "status=$status"
  echo "runtime_probe_status=$runtime_probe_status"
  echo "hits_file=$hits_file"
  echo "miss_file=$miss_file"
} >"$report"

{
  echo "backend_rawptr_surface_forbid_status=$status"
  echo "backend_rawptr_surface_forbid_runtime_probe_status=$runtime_probe_status"
  echo "backend_rawptr_surface_forbid_report=$report"
} >"$snapshot"

if [ "$status" != "ok" ]; then
  echo "[verify_backend_rawptr_surface_forbid] raw pointer surface forbid drift detected" 1>&2
  echo "  report: $report" 1>&2
  if [ -s "$miss_file" ]; then
    sed -n '1,120p' "$miss_file" 1>&2 || true
  fi
  exit 1
fi

echo "verify_backend_rawptr_surface_forbid ok"
