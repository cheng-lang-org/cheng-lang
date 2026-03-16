#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_backend_noptr_exemption_scope] rg is required" 1>&2
  exit 2
fi

out_dir="artifacts/backend_noptr_exemption_scope"
rm -rf "$out_dir"
mkdir -p "$out_dir"
hits="$out_dir/noptr_exemption_hits.txt"
bad="$out_dir/noptr_exemption_bad.txt"
report="$out_dir/verify_backend_noptr_exemption_scope.report.txt"
snapshot="$out_dir/verify_backend_noptr_exemption_scope.snapshot.env"

rm -f src/tooling/.tmp_backend_prod_closure.debug.sh src/tooling/.tmp_backend_prod_closure.debug.env || true
rg -n -P --glob '*.cheng' --glob '*.sh' '^(?![[:space:]]*#).*STAGE1_NO_POINTERS_NON_C_ABI(_INTERNAL)?=0' src >"$hits" || true
: >"$bad"

while IFS= read -r line; do
  [ "$line" != "" ] || continue
  file="${line%%:*}"
  case "$file" in
    src/tooling/cheng_tooling.cheng|src/tooling/cheng_tooling_embedded*.cheng|src/tooling/cheng_tooling_embedded_scripts/*.sh|src/tooling/.tmp_*|src/tooling/cheng_sidecar_rewrite_*.cheng)
      continue
      ;;
  esac
  printf '%s\n' "$line" >>"$bad"
done <"$hits"

if [ -s "$bad" ]; then
  echo "[Error] no-pointer exemption found outside allowlist:" 1>&2
  sed -n '1,200p' "$bad" | sed 's/^/  - /' 1>&2
  exit 1
fi

count="$(wc -l <"$hits" | tr -d '[:space:]')"
{
  echo "status=ok"
  echo "hit_count=$count"
  echo "hits=$hits"
  echo "allowlist=src/tooling/cheng_tooling.cheng,src/tooling/cheng_tooling_embedded*.cheng,src/tooling/cheng_tooling_embedded_scripts/*.sh,src/tooling/.tmp_*,src/tooling/cheng_sidecar_rewrite_*.cheng"
} >"$report"

{
  echo "backend_noptr_exemption_scope_status=ok"
  echo "backend_noptr_exemption_scope_hit_count=$count"
  echo "backend_noptr_exemption_scope_report=$report"
} >"$snapshot"

echo "verify_backend_noptr_exemption_scope ok"
