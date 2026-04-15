#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
bridge_env="$root/artifacts/v3_bootstrap/bootstrap.env"

sh "$root/v3/tooling/bootstrap_bridge_v3.sh" >/dev/null

if [ ! -f "$bridge_env" ]; then
  echo "v3 tooling: missing bootstrap env: $bridge_env" >&2
  exit 1
fi

# shellcheck disable=SC1090
. "$bridge_env"

if [ -z "${V3_BOOTSTRAP_STAGE3:-}" ] || [ ! -x "$V3_BOOTSTRAP_STAGE3" ]; then
  echo "v3 tooling: missing bootstrap stage3: ${V3_BOOTSTRAP_STAGE3:-}" >&2
  exit 1
fi

exec "$V3_BOOTSTRAP_STAGE3" build-backend-driver "$@"
