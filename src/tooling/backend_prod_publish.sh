#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_prod_publish.sh [--dst:<dir>] [backend_prod_closure_args...]

Notes:
  - Runs backend production closure and publishes the signed release into dist/.
  - Pass-through args are forwarded to backend_prod_closure.sh (e.g. --no-android-run, etc).
EOF
}

dst="dist/releases"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --dst:*)
      dst="${1#--dst:}"
      shift || true
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      break
      ;;
  esac
done

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

sh src/tooling/backend_prod_closure.sh "$@"

manifest="artifacts/backend_prod/release_manifest.json"
bundle="artifacts/backend_prod/backend_release.tar.gz"
sh src/tooling/backend_release_publish.sh --manifest:"$manifest" --bundle:"$bundle" --dst:"$dst"

echo "backend_prod_publish ok"
