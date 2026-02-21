#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

if ! command -v cc >/dev/null 2>&1; then
  echo "[Error] missing C compiler: cc" 1>&2
  exit 2
fi

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
src_file="$repo_root/src/tooling/profile_backend_sample.c"
out_file="${1:-$repo_root/artifacts/bin/profile_backend_sample}"

case "$out_file" in
  /*) ;;
  *) out_file="$PWD/$out_file" ;;
esac

mkdir -p "$(dirname "$out_file")"
cc -O2 -Wall -Wextra -pedantic -std=c11 "$src_file" -o "$out_file"
echo "$out_file"
