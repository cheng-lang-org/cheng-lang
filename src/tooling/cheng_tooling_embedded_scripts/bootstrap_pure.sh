#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  cat <<'EOF'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/bootstrap_pure.sh

Notes:
  - Retired.
  - Old backend driver bootstrap path has been removed.
EOF
  exit 0
fi

echo "[bootstrap_pure] retired: old backend driver bootstrap path has been removed" >&2
exit 2
