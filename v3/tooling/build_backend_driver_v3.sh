#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
bridge_env="$root/artifacts/v3_bootstrap/bootstrap.env"
backend_driver="$root/artifacts/v3_backend_driver/cheng"
bootstrap_driver="$root/artifacts/v3_backend_driver/cheng.bootstrap"
package_root="$root/v3"
compiler_entry="$root/v3/src/tooling/compiler_main.cheng"
compiler_runtime="$root/v3/src/tooling/compiler_runtime.cheng"
compiler_request="$root/v3/src/tooling/compiler_request.cheng"
bootstrap_contracts="$root/v3/src/tooling/bootstrap_contracts.cheng"
build_plan_source="$root/v3/src/backend/build_plan.cheng"

v3_host_target() {
  case "$(uname -s)" in
    Darwin)
      printf '%s\n' "$(uname -m)-apple-darwin"
      ;;
    Linux)
      printf '%s\n' "$(uname -m)-unknown-linux-gnu"
      ;;
    *)
      printf '%s\n' "arm64-apple-darwin"
      ;;
  esac
}

if [ "${CHENG_V3_TARGET:-}" = "" ]; then
  export CHENG_V3_TARGET="$(v3_host_target)"
fi

v3_bin_fresh() {
  bin="$1"
  shift
  [ -x "$bin" ] || return 1
  for src in "$@"; do
    if [ "$src" -nt "$bin" ]; then
      return 1
    fi
  done
  return 0
}

sh "$root/v3/tooling/bootstrap_bridge_v3.sh" >/dev/null

if [ ! -f "$bridge_env" ]; then
  echo "v3 tooling: missing bootstrap env: $bridge_env" >&2
  exit 1
fi

# shellcheck disable=SC1090
. "$bridge_env"

target_triple="$(uname -m)-$(uname -s | tr '[:upper:]' '[:lower:]')"

if v3_bin_fresh \
  "$backend_driver" \
  "$compiler_entry" \
  "$compiler_runtime" \
  "$compiler_request" \
  "$bootstrap_contracts" \
  "$build_plan_source"; then
  exec "$backend_driver" build-backend-driver "$@"
fi

if [ -z "${V3_BOOTSTRAP_STAGE3:-}" ] || [ ! -x "$V3_BOOTSTRAP_STAGE3" ]; then
  echo "v3 tooling: missing bootstrap stage3: ${V3_BOOTSTRAP_STAGE3:-}" >&2
  exit 1
fi

case "$(uname -s)" in
  Darwin)
    target_triple="$(uname -m)-apple-darwin"
    ;;
  Linux)
    target_triple="$(uname -m)-unknown-linux-gnu"
    ;;
esac

if ! v3_bin_fresh \
  "$bootstrap_driver" \
  "$compiler_entry" \
  "$compiler_runtime" \
  "$compiler_request" \
  "$bootstrap_contracts" \
  "$build_plan_source"; then
  "$V3_BOOTSTRAP_STAGE3" system-link-exec \
    --root:"$package_root" \
    --in:"$compiler_entry" \
    --emit:exe \
    --target:"$target_triple" \
    --out:"$bootstrap_driver" \
    --report-out:"$root/artifacts/v3_backend_driver/build_backend_driver_v3.bootstrap.report.txt"
fi

exec "$bootstrap_driver" build-backend-driver "$@"
