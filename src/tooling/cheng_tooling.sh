#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

src="src/tooling/cheng_tooling.cheng"
bin="${TOOLING_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
force_build="${TOOLING_FORCE_BUILD:-0}"
tooling_linker="${TOOLING_LINKER:-system}"
build_driver="${TOOLING_BUILD_DRIVER:-}"

if [ ! -f "$src" ]; then
  echo "[cheng_tooling] source not found: $src" 1>&2
  exit 1
fi

need_build="0"
if [ ! -x "$bin" ]; then
  need_build="1"
elif [ "$src" -nt "$bin" ]; then
  need_build="1"
fi
case "$force_build" in
  1|true|TRUE|yes|YES|on|ON) need_build="1" ;;
esac

if [ "$need_build" = "1" ]; then
  mkdir -p "$(dirname "$bin")"
  if [ "$build_driver" = "" ]; then
    if [ -x "artifacts/backend_seed/cheng.stage2" ]; then
      build_driver="artifacts/backend_seed/cheng.stage2"
    else
      build_driver="$(sh src/tooling/backend_driver_path.sh)"
    fi
  fi
  # tooling launcher itself uses argv pointer entry; build it under non-C-ABI pointer exemption.
  BACKEND_DRIVER="$build_driver" \
  STAGE1_NO_POINTERS_NON_C_ABI=0 \
  STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0 \
  sh src/tooling/chengc.sh "$src" --out:"$bin" --abi:v2_noptr --linker:"$tooling_linker"
fi

export TOOLING_ROOT="$root"
exec "$bin" "$@"
