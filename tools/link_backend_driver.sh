#!/bin/bash
# Link cold-compiled backend driver .o files into final binary.
# Usage: bash tools/link_backend_driver.sh [--install]
set -euo pipefail
cd "$(dirname "$0")/.."

INSTALL=false
if [ "${1:-}" = "--install" ]; then INSTALL=true; fi

LATEST=$(ls -dt artifacts/backend_driver/builds/pid-*/ 2>/dev/null | head -1)
if [ -z "$LATEST" ]; then
    echo "No build directory found"
    exit 1
fi

PRIMARY=$(ls "$LATEST/cheng.build_candidate.primary.o" 2>/dev/null)
if [ -z "$PRIMARY" ]; then
    echo "No primary.o in $LATEST"
    exit 2
fi

PROVIDERS=$(ls "$LATEST/cheng.build_candidate.provider."*.o 2>/dev/null)
if [ -z "$PROVIDERS" ]; then
    echo "No provider .o files in $LATEST"
    exit 3
fi

OUT="/tmp/ct_backend_driver_linked"
cc -o "$OUT" $PRIMARY $PROVIDERS 2>&1
echo "Linked: $(wc -c < "$OUT") bytes ($LATEST)"

# Quick smoke test
if timeout 5 "$OUT" help >/dev/null 2>&1; then
    echo "Smoke test: PASS"
else
    echo "Smoke test: FAIL (exit=$?)"
fi

if $INSTALL; then
    cp "$OUT" artifacts/backend_driver/cheng
    echo "Installed to artifacts/backend_driver/cheng"
fi
