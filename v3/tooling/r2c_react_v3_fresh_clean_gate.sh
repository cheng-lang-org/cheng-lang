#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"

sh "$root/v3/tooling/bootstrap_bridge_v3.sh" >/dev/null
sh "$root/v3/tooling/build_backend_driver_v3.sh" >/dev/null
exec "$root/artifacts/v3_backend_driver/cheng" r2c-react-v3-fresh-clean-gate "$@"
