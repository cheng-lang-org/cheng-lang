#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
bridge_env="$root/artifacts/v3_bootstrap/bootstrap.env"
out_dir="$root/artifacts/v3_windows_builtin_verify"
hello_src="$root/tests/cheng/backend/fixtures/hello_importc_puts.cheng"

usage() {
  cat <<'EOF'
Usage:
  v3/tooling/verify_windows_builtin_linker_v3.sh

Notes:
  - Verifies the v3 seed's pure built-in Windows AArch64 consteval executable path.
  - Does not depend on any host Windows linker or the repo main backend driver.
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
    echo "[v3 windows builtin] unknown arg: $1" >&2
    usage >&2
    exit 2
    ;;
esac

ensure_bridge() {
  if [ ! -f "$bridge_env" ]; then
    sh "$root/v3/tooling/bootstrap_bridge_v3.sh"
  fi
  if [ ! -f "$bridge_env" ]; then
    echo "[v3 windows builtin] missing bootstrap env: $bridge_env" >&2
    exit 1
  fi
  # shellcheck disable=SC1090
  . "$bridge_env"
  if [ ! -x "${V3_BOOTSTRAP_STAGE2:-}" ]; then
    echo "[v3 windows builtin] missing stage2 bootstrap binary" >&2
    exit 1
  fi
}

build_one() {
  name="$1"
  src="$root/tests/cheng/backend/fixtures/$2.cheng"
  exe="$out_dir/$name.exe"
  report="$out_dir/$name.report"
  log="$out_dir/$name.log"
  "$V3_BOOTSTRAP_STAGE2" system-link-exec \
    --in:"$src" \
    --emit:exe \
    --target:aarch64-pc-windows-msvc \
    --out:"$exe" \
    --report-out:"$report" >"$log" 2>&1
}

ensure_bridge
mkdir -p "$out_dir"

build_one hello_importc_puts hello_importc_puts

for name in \
  return_add \
  return_call \
  return_if_stmt \
  return_while_sum \
  return_and_expr \
  return_bitwise \
  return_shift \
  return_for_sum \
  return_while_break \
  return_while_continue \
  return_inline_if_and_ternary \
  return_call_mixed \
  return_float32_arith_chain \
  return_float32_roundtrip \
  return_float64_ops \
  return_float_compare_cast \
  return_float_mixed_int_cast \
  return_global_add \
  return_global_assign \
  return_cast_i32 \
  return_cast_i64 \
  return_std_os_getenv_fileexists
do
  build_one "$name" "$name"
done

file "$out_dir/hello_importc_puts.exe" | grep -F "PE32+ executable (console) Aarch64, for MS Windows" >/dev/null
objdump -f "$out_dir/hello_importc_puts.exe" | grep -F "file format coff-arm64" >/dev/null
objdump -p "$out_dir/hello_importc_puts.exe" | grep -F "DLL Name: UCRTBASE.dll" >/dev/null
objdump -p "$out_dir/hello_importc_puts.exe" | grep -F "puts" >/dev/null
objdump -p "$out_dir/hello_importc_puts.exe" | grep -F "DLL Name: KERNEL32.dll" >/dev/null
objdump -p "$out_dir/return_add.exe" | grep -F "ExitProcess" >/dev/null
grep -F "native_linker_program=internal_pe_consteval_linker" "$out_dir/hello_importc_puts.report" >/dev/null
grep -F "native_linker_flavor=pe_aarch64_consteval_internal" "$out_dir/hello_importc_puts.report" >/dev/null
grep -F "native_link_input_count=0" "$out_dir/hello_importc_puts.report" >/dev/null
grep -F "native_link_missing_reasons=-" "$out_dir/hello_importc_puts.report" >/dev/null
for name in \
  return_add \
  return_call \
  return_if_stmt \
  return_while_sum \
  return_and_expr \
  return_bitwise \
  return_shift \
  return_for_sum \
  return_while_break \
  return_while_continue \
  return_inline_if_and_ternary \
  return_call_mixed \
  return_float32_arith_chain \
  return_float32_roundtrip \
  return_float64_ops \
  return_float_compare_cast \
  return_float_mixed_int_cast \
  return_global_add \
  return_global_assign \
  return_cast_i32 \
  return_cast_i64 \
  return_std_os_getenv_fileexists
do
  file "$out_dir/$name.exe" | grep -F "PE32+ executable (console) Aarch64, for MS Windows" >/dev/null
  grep -F "native_linker_program=internal_pe_consteval_linker" "$out_dir/$name.report" >/dev/null
  grep -F "native_link_input_count=0" "$out_dir/$name.report" >/dev/null
  grep -F "native_link_missing_reasons=-" "$out_dir/$name.report" >/dev/null
done

echo "verify_windows_builtin_linker_v3 ok"
