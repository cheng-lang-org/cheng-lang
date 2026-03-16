#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_emit_obj_contract] rg is required" >&2
  exit 2
fi

out_dir="artifacts/backend_emit_obj_contract"
rm -rf "$out_dir"
mkdir -p "$out_dir"

emit_scripts="$out_dir/backend_emit_obj.scripts.txt"
missing_bridge="$out_dir/backend_emit_obj.missing_bridge.txt"
bad_allow="$out_dir/backend_emit_obj.bad_allow.txt"
report="$out_dir/backend_emit_obj_contract.report.txt"
bridge_file="src/tooling/env_prefix_bridge.sh"

rg -l --glob '*.sh' --glob 'cheng_tooling_embedded_inline.cheng' "BACKEND_EMIT=obj" src/tooling \
  | rg -v 'verify_backend_emit_obj_contract(\.inline|\.sh)?$' \
  | LC_ALL=C sort >"$emit_scripts"

if [ ! -s "$emit_scripts" ]; then
  echo "[verify_backend_emit_obj_contract] no tooling scripts use BACKEND_EMIT=obj" >&2
  exit 1
fi

: >"$missing_bridge"
: >"$bad_allow"
while IFS= read -r file; do
  [ "$file" != "" ] || continue
  if [ "$file" != "src/tooling/cheng_tooling_embedded_inline.cheng" ] && ! rg -q "env_prefix_bridge.sh" "$file"; then
    printf '%s\n' "$file" >>"$missing_bridge"
  fi
  if rg -q '^[[:space:]]*(export[[:space:]]+)?(BACKEND_INTERNAL_ALLOW_EMIT_OBJ|BACKEND_INTERNAL_ALLOW_EMIT_OBJ)=0([[:space:]]|$)' "$file"; then
    printf '%s\n' "$file" >>"$bad_allow"
  fi
done <"$emit_scripts"

if [ -s "$missing_bridge" ]; then
  echo "[verify_backend_emit_obj_contract] scripts using BACKEND_EMIT=obj must source env_prefix_bridge.sh" >&2
  sed -n '1,200p' "$missing_bridge" >&2
  exit 1
fi
if [ -s "$bad_allow" ]; then
  echo "[verify_backend_emit_obj_contract] scripts using BACKEND_EMIT=obj must not disable BACKEND_INTERNAL_ALLOW_EMIT_OBJ" >&2
  sed -n '1,200p' "$bad_allow" >&2
  exit 1
fi

if ! rg -q 'BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1' "$bridge_file"; then
  echo "[verify_backend_emit_obj_contract] env_prefix_bridge missing BACKEND_INTERNAL_ALLOW_EMIT_OBJ central gate" >&2
  exit 1
fi
if ! rg -q 'BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1' "$bridge_file"; then
  echo "[verify_backend_emit_obj_contract] env_prefix_bridge missing BACKEND_INTERNAL_ALLOW_EMIT_OBJ central gate" >&2
  exit 1
fi

emit_count="$(wc -l <"$emit_scripts" | tr -d ' ')"
explicit_allow_lines="$(rg -n --glob '*.sh' --glob 'cheng_tooling_embedded_inline.cheng' 'BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1|BACKEND_INTERNAL_ALLOW_EMIT_OBJ=1' src/tooling | wc -l | tr -d ' ')"

{
  echo "verify_backend_emit_obj_contract report"
  echo "status=ok"
  echo "emit_obj_script_count=$emit_count"
  echo "missing_bridge_count=0"
  echo "bad_allow_count=0"
  echo "explicit_allow_lines=$explicit_allow_lines"
  echo "emit_scripts=$emit_scripts"
  echo "bridge_file=$bridge_file"
} >"$report"

echo "verify_backend_emit_obj_contract ok"
