#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT"

fail=0
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

check_nonempty_list() {
  title="$1"
  if [ ! -s "$tmp" ]; then
    return 0
  fi
  fail=1
  echo "[verify_no_root_build_artifacts] $title" 1>&2
  cat "$tmp" 1>&2
  : >"$tmp"
}

find . -maxdepth 1 -type f \( -name '*.o' -o -name '*.obj' -o -name '*.out' \) -print \
  | sed 's#^\./##' >"$tmp"
check_nonempty_list "unexpected object/temp files in repo root:"

find . -maxdepth 1 -type d \( -name '*.objs' -o -name '*.objs.lock' \) -print \
  | sed 's#^\./##' >"$tmp"
check_nonempty_list "unexpected object directories in repo root:"

find . -maxdepth 1 -type f -print0 \
  | while IFS= read -r -d '' f; do
      kind="$(file -b "$f" 2>/dev/null || true)"
      case "$kind" in
        *Mach-O*|*ELF*|*PE32*|*"current ar archive"*)
          printf '%s\n' "${f#./}"
          ;;
      esac
    done >"$tmp"
check_nonempty_list "unexpected compiled binaries in repo root:"

if [ "$fail" -ne 0 ]; then
  echo "[verify_no_root_build_artifacts] fix: move outputs to artifacts/chengc or chengcache" 1>&2
  exit 1
fi

echo "verify ok"
