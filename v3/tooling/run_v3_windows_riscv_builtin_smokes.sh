#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"

usage() {
  cat <<'EOF'
Usage:
  v3/tooling/run_v3_windows_riscv_builtin_smokes.sh

Notes:
  - Runs the v3 seed pure built-in Windows and RISC-V cross-target smoke gates.
EOF
}

case "${1:-}" in
  --help|-h)
    usage
    exit 0
    ;;
  "")
    ;;
  *)
    echo "[v3 cross builtin] unknown arg: $1" >&2
    usage >&2
    exit 2
    ;;
esac

sh "$root/v3/tooling/verify_windows_builtin_linker_v3.sh"
sh "$root/v3/tooling/verify_riscv64_builtin_linker_v3.sh"

echo "run_v3_windows_riscv_builtin_smokes ok"
