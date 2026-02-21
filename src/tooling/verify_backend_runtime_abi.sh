#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

header_file="src/runtime/native/system_helpers.h"
c_runtime_main="src/runtime/native/system_helpers.c"
c_runtime_float_bits="src/runtime/native/system_helpers_float_bits.c"
backend_runtime_file="src/std/system_helpers_backend.cheng"

fail() {
  echo "[verify_backend_runtime_abi] $1" 1>&2
  exit 1
}

require_file() {
  file="$1"
  [ -f "$file" ] || fail "missing file: $file"
}

extract_header_symbols() {
  rg -N --no-filename '^[[:space:]]*[A-Za-z_][A-Za-z0-9_[:space:]\*]*[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\([^;]*\);[[:space:]]*$' "$1" \
    | sed -E 's/^[[:space:]]*[A-Za-z_][A-Za-z0-9_[:space:]\*]*[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*\(.*/\1/' \
    | sort -u
}

extract_c_defined_symbols() {
  rg -N --no-filename '^[[:space:]]*(__attribute__\(\([^)]*\)\)[[:space:]]*)*[A-Za-z_][A-Za-z0-9_[:space:]\*]*[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\([^;]*\)[[:space:]]*\{' "$@" \
    | sed -E 's/^[[:space:]]*(__attribute__\(\([^)]*\)\)[[:space:]]*)*//' \
    | sed -E 's/^[[:space:]]*[A-Za-z_][A-Za-z0-9_[:space:]\*]*[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*\(.*/\1/' \
    | sort -u
}

extract_backend_symbols() {
  rg -N --no-filename '^[[:space:]]*fn[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(' "$1" \
    | sed -E 's/^[[:space:]]*fn[[:space:]]+([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*\(.*/\1/' \
    | sort -u
}

tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/cheng_runtime_abi.XXXXXX")"
cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

require_file "$header_file"
require_file "$c_runtime_main"
require_file "$backend_runtime_file"

header_symbols="$tmp_dir/header_symbols.txt"
c_symbols="$tmp_dir/c_symbols.txt"
backend_symbols="$tmp_dir/backend_symbols.txt"
missing_in_c="$tmp_dir/missing_in_c.txt"
missing_in_backend="$tmp_dir/missing_in_backend.txt"
allowed_backend_missing="$tmp_dir/allowed_backend_missing.txt"
unexpected_backend_missing="$tmp_dir/unexpected_backend_missing.txt"

extract_header_symbols "$header_file" > "$header_symbols"

if [ -f "$c_runtime_float_bits" ]; then
  extract_c_defined_symbols "$c_runtime_main" "$c_runtime_float_bits" > "$c_symbols"
else
  extract_c_defined_symbols "$c_runtime_main" > "$c_symbols"
fi

extract_backend_symbols "$backend_runtime_file" > "$backend_symbols"

comm -23 "$header_symbols" "$c_symbols" > "$missing_in_c"
if [ -s "$missing_in_c" ]; then
  echo "[verify_backend_runtime_abi] C runtime missing header symbols:" 1>&2
  cat "$missing_in_c" 1>&2
  exit 1
fi

cat > "$allowed_backend_missing" <<'EOF'
alloc
bitand_0
bitnot_0
bitor_0
cheng_bits_to_f32
cheng_bits_to_f64
cheng_errno
cheng_f32_to_bits
cheng_f64_ge_bits
cheng_f64_gt_bits
cheng_f64_le_bits
cheng_f64_lt_bits
cheng_f64_sub_bits
cheng_f64_to_bits
cheng_jpeg_decode
cheng_jpeg_free
cheng_memcpy
cheng_memcpy_ffi
cheng_memset
cheng_memset_ffi
cheng_ptr_size
cheng_ptr_to_u64
cheng_read_file
cheng_seq_get
cheng_seq_set
cheng_slice_get
cheng_slice_set
cheng_strcmp
cheng_strerror
cheng_strlen
cheng_tcp_listener
cheng_write_file
copyMem
div_0
load_ptr
mod_0
mul_0
not_0
ptr_add
setMem
shl_0
shr_0
store_ptr
sys_memset
xor_0
EOF
sort -u "$allowed_backend_missing" -o "$allowed_backend_missing"

comm -23 "$header_symbols" "$backend_symbols" > "$missing_in_backend"
comm -23 "$missing_in_backend" "$allowed_backend_missing" > "$unexpected_backend_missing"
if [ -s "$unexpected_backend_missing" ]; then
  echo "[verify_backend_runtime_abi] backend runtime missing unexpected header symbols:" 1>&2
  cat "$unexpected_backend_missing" 1>&2
  exit 1
fi

for alias in __addr _addr; do
  if ! grep -qx "$alias" "$c_symbols"; then
    fail "C runtime missing alias symbol: $alias"
  fi
  if ! grep -qx "$alias" "$backend_symbols"; then
    fail "backend runtime missing alias symbol: $alias"
  fi
done

echo "verify_backend_runtime_abi ok"
