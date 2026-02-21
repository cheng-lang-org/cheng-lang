#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
builder="$repo_root/src/tooling/build_profile_backend_sample.sh"
src_c="$repo_root/src/tooling/profile_backend_sample.c"
bin_path="${PROFILE_SAMPLE_BIN:-$repo_root/artifacts/bin/profile_backend_sample}"

case "$bin_path" in
  /*) ;;
  *) bin_path="$PWD/$bin_path" ;;
esac

need_build="0"
if [ ! -x "$bin_path" ]; then
  need_build="1"
elif [ "$src_c" -nt "$bin_path" ] || [ "$builder" -nt "$bin_path" ]; then
  need_build="1"
fi

if [ "$need_build" = "1" ]; then
  sh "$builder" "$bin_path" >/dev/null
fi

exec "$bin_path" "$@"
