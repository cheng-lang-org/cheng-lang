#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

echo "[verify_backend_selfhost_bootstrap] retired: old backend driver bootstrap surface has been removed" >&2
echo "verify_backend_selfhost_bootstrap ok"
