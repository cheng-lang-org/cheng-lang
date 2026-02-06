#!/usr/bin/env sh
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  src/tooling/verify_c_backend_intbits.sh [--example:<path>] [--name:<base>] [--out-dir:<dir>] [--cc:<path>]

Notes:
  - Verifies direct C emit maps int/uint to NI/NU.
  - Checks CHENG_INTBITS=32/64 preprocessing resolves NI/NU typedefs correctly.
EOF
}

example="${CHENG_C_INTBITS_EXAMPLE:-examples/c_backend_intbits.cheng}"
name="${CHENG_C_INTBITS_NAME:-c_backend_intbits}"
out_dir="${CHENG_C_INTBITS_OUT_DIR:-chengcache/c_backend_intbits}"
cc="${CHENG_C_CC:-${CC:-cc}}"
runtime_inc="${CHENG_RT_INC:-${CHENG_RUNTIME_INC:-runtime/include}}"

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
    --cc:*)
      cc="${1#--cc:}"
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

if ! command -v "$cc" >/dev/null 2>&1; then
  echo "[Error] compiler not found: $cc" 1>&2
  exit 2
fi

check_grep() {
  pattern="$1"
  file="$2"
  if command -v rg >/dev/null 2>&1; then
    if ! rg -q "$pattern" "$file"; then
      echo "[Error] expected pattern not found: $pattern ($file)" 1>&2
      exit 2
    fi
  else
    if ! grep -q "$pattern" "$file"; then
      echo "[Error] expected pattern not found: $pattern ($file)" 1>&2
      exit 2
    fi
  fi
}

mkdir -p "$out_dir"
c_out="$out_dir/${name}.c"
rm -f "$c_out"

sh src/tooling/chengc.sh "$example" --name:"$name" --emit-c --c-out:"$c_out"

check_grep "NI[[:space:]]+a_int" "$c_out"
check_grep "NU[[:space:]]+b_uint" "$c_out"
check_grep "NI[[:space:]]+c_infer" "$c_out"

for bits in 32 64; do
  pp_out="$out_dir/${name}.intbits${bits}.i"
  "$cc" -E -P -DCHENG_INTBITS=$bits -I"$runtime_inc" -Isrc/runtime/native "$c_out" >"$pp_out"
  check_grep "typedef NI${bits} NI;" "$pp_out"
  check_grep "typedef NU${bits} NU;" "$pp_out"
done

echo "verify_c_backend_intbits ok"
