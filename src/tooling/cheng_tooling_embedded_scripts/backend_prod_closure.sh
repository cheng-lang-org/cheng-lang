#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

native_bin="${TOOLING_NATIVE_BIN:-}"
if [ "$native_bin" = "" ]; then
  if [ -x "$root/artifacts/tooling_bundle/full/cheng_tooling" ]; then
    native_bin="$root/artifacts/tooling_bundle/full/cheng_tooling"
  elif [ -x "$root/artifacts/tooling_bundle/core/cheng_tooling_global" ]; then
    native_bin="$root/artifacts/tooling_bundle/core/cheng_tooling_global"
  else
    echo "[backend_prod_closure] missing native tooling binary" 1>&2
    exit 1
  fi
fi

if [ ! -x "$native_bin" ]; then
  echo "[backend_prod_closure] native tooling binary not executable: $native_bin" 1>&2
  exit 1
fi

# `verify_tooling_cmdline` now exercises repo/global/real launcher paths and
# no longer fits the historical 60s closure-wide gate budget.
if [ "${BACKEND_PROD_GATE_TIMEOUT:-}" = "" ]; then
  export BACKEND_PROD_GATE_TIMEOUT=300
fi

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "help" ]; then
  exec "$native_bin" backend_prod_closure "$@"
fi

if [ "${BACKEND_DRIVER:-}" = "" ]; then
  resolver="$root/src/tooling/cheng_tooling_embedded_scripts/backend_driver_path.sh"
  if [ ! -x "$resolver" ]; then
    echo "[backend_prod_closure] missing backend driver resolver: $resolver" 1>&2
    exit 1
  fi
  resolved_driver="$(
    BACKEND_DRIVER_ALLOW_FALLBACK=1 \
    BACKEND_DRIVER_PATH_ALLOW_SELFHOST="${BACKEND_DRIVER_PATH_ALLOW_SELFHOST:-0}" \
    BACKEND_DRIVER_PATH_PREFER_REBUILD="${BACKEND_DRIVER_PATH_PREFER_REBUILD:-1}" \
    sh "$resolver"
  )"
  if [ "$resolved_driver" = "" ]; then
    echo "[backend_prod_closure] failed to resolve backend driver" 1>&2
    exit 1
  fi
export BACKEND_DRIVER="$resolved_driver"
fi

export TOOLING_EXEC_WRAPPER_CALLER="backend_prod_closure"
exec "$native_bin" backend_prod_closure "$@"
