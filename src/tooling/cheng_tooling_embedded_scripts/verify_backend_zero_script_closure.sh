#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

fail() {
  echo "[verify_backend_zero_script_closure] $1" 1>&2
  exit 1
}

if ! command -v rg >/dev/null 2>&1; then
  fail "rg is required"
fi

embedded_map="src/tooling/cheng_tooling_embedded_inline.cheng"
tool="${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling}"
for required in "$embedded_map" "$tool"; do
  [ -f "$required" ] || fail "missing required file: $required"
done

left_tooling="$(find src/tooling -maxdepth 1 -type f -name '*.sh' | LC_ALL=C sort || true)"
if [ "$left_tooling" != "" ]; then
  echo "[verify_backend_zero_script_closure] unexpected shell wrappers under src/tooling:" 1>&2
  printf '%s\n' "$left_tooling" | sed 's/^/  - /' 1>&2
  exit 1
fi

left_scripts="$(find scripts -maxdepth 1 -type f -name '*.sh' | LC_ALL=C sort || true)"
if [ "$left_scripts" != "" ]; then
  echo "[verify_backend_zero_script_closure] unexpected shell wrappers under scripts/:" 1>&2
  printf '%s\n' "$left_scripts" | sed 's/^/  - /' 1>&2
  exit 1
fi

out_dir="artifacts/backend_zero_script_closure"
mkdir -p "$out_dir"
map_ids="$out_dir/map_ids.txt"
"$tool" embedded-ids | LC_ALL=C sort -u >"$map_ids"

tmp_body="$out_dir/.tmp.script_body.sh"
while IFS= read -r sid; do
  [ "$sid" != "" ] || continue
  "$tool" embedded-text --id:"$sid" >"$tmp_body"
  if rg -n '(^|[[:space:]])(sh|bash)[[:space:]]+src/tooling/[A-Za-z0-9_.-]+\.sh' -P "$tmp_body" >/dev/null 2>&1; then
    echo "[verify_backend_zero_script_closure] embedded script still contains direct wrapper invocation: $sid" 1>&2
    rg -n '(^|[[:space:]])(sh|bash)[[:space:]]+src/tooling/[A-Za-z0-9_.-]+\.sh' -P "$tmp_body" | sed 's/^/  - /' 1>&2
    exit 1
  fi
done <"$map_ids"
rm -f "$tmp_body"

report="$out_dir/backend_zero_script_closure.report.txt"
{
  echo "status=ok"
  echo "tooling_shell_files=0"
  echo "allowed_tooling_shells="
  echo "scripts_shell_files=0"
  echo "map_ids=$map_ids"
} >"$report"

echo "verify_backend_zero_script_closure ok"
