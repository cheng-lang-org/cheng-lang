#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

contains_fuse_ld_flag() {
  case "${1:-}" in
    *-fuse-ld=*) return 0 ;;
  esac
  return 1
}

tool_available() {
  command -v "$1" >/dev/null 2>&1
}

ldflags_hint="${BACKEND_LDFLAGS:-}"
while [ "${1:-}" != "" ]; do
  case "$1" in
    --ldflags:*)
      ldflags_hint="${1#--ldflags:}"
      ;;
    *)
      echo "[resolve_system_linker] unknown arg: $1" 1>&2
      exit 2
      ;;
  esac
  shift || true
done

if [ "${BACKEND_LD:-}" != "" ]; then
  echo "kind=explicit_ld"
  echo "ldflags="
  exit 0
fi

if contains_fuse_ld_flag "$ldflags_hint"; then
  echo "kind=user_ldflags"
  echo "ldflags="
  exit 0
fi

priority_raw="${BACKEND_SYSTEM_LINKER_PRIORITY:-mold,lld,default}"
priority_csv="$(printf '%s' "$priority_raw" | tr ';' ',' | tr '[:upper:]' '[:lower:]')"
kind="default"
ldflags=""

old_ifs="$IFS"
IFS=','
for token in $priority_csv; do
  case "$token" in
    ""|" ")
      continue
      ;;
    mold)
      if tool_available mold || tool_available ld.mold; then
        kind="mold"
        ldflags="-fuse-ld=mold"
        break
      fi
      ;;
    lld)
      if tool_available ld.lld || tool_available lld || tool_available llvm-lld; then
        kind="lld"
        ldflags="-fuse-ld=lld"
        break
      fi
      ;;
    default)
      kind="default"
      ldflags=""
      break
      ;;
    *)
      ;;
  esac
done
IFS="$old_ifs"

echo "kind=$kind"
echo "ldflags=$ldflags"
