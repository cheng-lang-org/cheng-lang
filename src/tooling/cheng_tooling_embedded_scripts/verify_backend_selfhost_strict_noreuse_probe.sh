#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

echo "[verify_backend_selfhost_strict_noreuse_probe] retired: old backend driver strict noreuse probe has been removed" >&2
echo "verify_backend_selfhost_strict_noreuse_probe ok"
