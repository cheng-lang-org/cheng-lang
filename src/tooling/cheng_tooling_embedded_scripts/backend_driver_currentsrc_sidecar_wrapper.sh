#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  cat <<'EOF'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/backend_driver_currentsrc_sidecar_wrapper.sh

Notes:
  - Retired.
  - Old backend driver sidecar wrapper has been removed.
EOF
  exit 0
fi

echo "[backend_driver_currentsrc_sidecar_wrapper] retired: old backend driver sidecar wrapper has been removed" >&2
exit 2
