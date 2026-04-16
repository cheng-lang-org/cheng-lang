#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  cat <<'EOF'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh

Notes:
  - Retired.
  - Old backend driver sidecar defaults have been removed.
EOF
  exit 0
fi

echo "[resolve_backend_sidecar_defaults] retired: old backend driver sidecar defaults have been removed" >&2
exit 2
