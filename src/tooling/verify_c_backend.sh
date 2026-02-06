#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_c_backend.sh [--example:<path>] [--name:<exe>] [--out-dir:<dir>]

Notes:
  - Verifies direct C backend can emit and link a minimal program.
  - Uses chengc.sh --backend:c (requires stage1_runner).
EOF
}

example="${CHENG_C_EXAMPLE:-examples/c_backend_smoke.cheng}"
name="${CHENG_C_NAME:-c_backend_smoke}"
out_dir="${CHENG_C_OUT_DIR:-chengcache/c_backend}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --example:*)
      example="${1#--example:}"
      ;;
    --name:*)
      name="${1#--name:}"
      ;;
    --out-dir:*)
      out_dir="${1#--out-dir:}"
      ;;
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

if [ ! -f "$example" ]; then
  echo "[Error] missing example: $example" 1>&2
  exit 2
fi

mkdir -p "$out_dir"
rm -f "$out_dir/$name"

CHENG_C_SYSTEM=1 CHENG_EMIT_C_MODULES=0 CHENG_DEPS_MODE=deps \
  sh src/tooling/chengc.sh "$example" --name:"$name" --backend:c

if [ ! -f "$name" ]; then
  echo "[Error] missing output binary: $name" 1>&2
  exit 2
fi

set +e
"./$name"
code="$?"
set -e
if [ "$code" -ne 7 ]; then
  echo "[Error] unexpected exit code: $code (expected 7)" 1>&2
  exit 2
fi

mv -f "$name" "$out_dir/$name"

echo "verify_c_backend ok"
