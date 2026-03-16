#!/usr/bin/env sh
:
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

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

tool="${TOOLING_SELF_BIN:-$root/artifacts/tooling_cmd/cheng_tooling}"
case "$tool" in
  /*) ;;
  *) tool="$root/$tool" ;;
esac

"$tool" backend_prod_closure "$@"

manifest="artifacts/backend_prod/release_manifest.json"
bundle="artifacts/backend_prod/backend_release.tar.gz"
"$tool" backend_release_publish --manifest:"$manifest" --bundle:"$bundle" --dst:"$dst"

echo "backend_prod_publish ok"
