#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_c_backend_full.sh

Notes:
  - Runs the non-std direct C backend regression set.
  - Aggregates verify_c_backend.sh, verify_c_backend_intbits.sh, and verify_c_backend_hybrid.sh.
EOF
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

sh src/tooling/verify_c_backend.sh
sh src/tooling/verify_c_backend_intbits.sh
sh src/tooling/verify_c_backend_hybrid.sh

echo "verify_c_backend_full ok"
