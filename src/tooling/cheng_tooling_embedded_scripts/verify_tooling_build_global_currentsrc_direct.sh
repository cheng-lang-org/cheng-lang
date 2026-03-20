#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

driver="${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_DRIVER:-}"
if [ "${1:-}" != "" ]; then
  case "$1" in
    --driver:*)
      driver="${1#--driver:}"
      ;;
    *)
      echo "[verify_tooling_build_global_currentsrc_direct] unknown arg: $1" 1>&2
      exit 2
      ;;
  esac
fi

if [ -z "$driver" ]; then
  echo "[verify_tooling_build_global_currentsrc_direct] missing explicit current-source driver" 1>&2
  echo "set TOOLING_BUILD_GLOBAL_CURRENTSOURCE_DRIVER or pass --driver:<path>" 1>&2
  exit 2
fi

out="${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_OUT:-/tmp/cheng_tooling.currentsrc_direct_probe.bin}"

env -i \
  PATH="${PATH:-/usr/bin:/bin:/usr/sbin:/sbin}" \
  HOME="${HOME:-$root}" \
  TOOLING_STAGE0_ALLOW_EXPLICIT=1 \
  TOOLING_TIMEOUT_DIAG=1 \
  TOOLING_TIMEOUT_DIAG_SUMMARY=1 \
  TOOLING_TIMEOUT_DIAG_SUMMARY_TOP="${TOOLING_TIMEOUT_DIAG_SUMMARY_TOP:-16}" \
  "$root/artifacts/tooling_cmd/cheng_tooling" \
  build-global \
  --out:"$out" \
  --linker:self \
  --driver:"$driver"
