#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
src="$root/tests/cheng/backend/fixtures/return_add.cheng"
out_dir="$root/artifacts/v3_debug_tools_verify"
obj_path="$out_dir/return_add_linux_aarch64.o"
debug_report="$out_dir/debug-report.txt"
symbols_report="$out_dir/symbols.txt"
line_map_report="$out_dir/line-map.txt"
elf_report="$out_dir/elf.txt"
build_log="$out_dir/build.log"

mkdir -p "$out_dir"

sh "$root/v3/tooling/bootstrap_bridge_v3.sh"

sh "$root/v3/tooling/cheng_v3.sh" debug-report \
  --root:"$root" \
  --in:"$src" \
  --emit:obj \
  --target:aarch64-unknown-linux-gnu \
  --out:"$obj_path" \
  --report-out:"$debug_report" >/dev/null

grep -q '^entry='"$src"'$' "$debug_report"
grep -q '^lowering_function_count=' "$debug_report"
grep -q '^primary_object_item_count=' "$debug_report"
grep -q '^native_linker_program=' "$debug_report"

sh "$root/v3/tooling/cheng_v3.sh" print-symbols \
  --root:"$root" \
  --in:"$src" \
  --emit:obj \
  --target:aarch64-unknown-linux-gnu \
  --out:"$obj_path" \
  --report-out:"$symbols_report" >/dev/null

grep -q '^v3_symbols_v1$' "$symbols_report"
grep -q '^lowering_symbol_count=' "$symbols_report"
grep -Eq '^lowering_symbol\[[0-9]+\]=' "$symbols_report"
grep -q '^primary_symbol_count=' "$symbols_report"

sh "$root/v3/tooling/cheng_v3.sh" print-line-map \
  --root:"$root" \
  --in:"$src" \
  --emit:obj \
  --target:aarch64-unknown-linux-gnu \
  --out:"$obj_path" \
  --report-out:"$line_map_report" >/dev/null

grep -q '^v3_line_map_v1$' "$line_map_report"
grep -q '^entry_count=' "$line_map_report"
grep -Eq '^entry	' "$line_map_report"

sh "$root/v3/tooling/cheng_v3.sh" debug-report \
  --root:"$root" \
  --in:"$src" \
  --emit:obj \
  --target:aarch64-unknown-linux-gnu >/dev/null

if ! "$root/artifacts/v3_bootstrap/cheng.stage3" system-link-exec \
  --contract-in:"$root/v3/bootstrap/stage1_bootstrap.cheng" \
  --root:"$root" \
  --in:"$src" \
  --emit:obj \
  --target:aarch64-unknown-linux-gnu \
  --out:"$obj_path" >"$build_log" 2>&1; then
  echo "v3 debug tools: failed to build ELF object log=$build_log" >&2
  tail -n 80 "$build_log" >&2 || true
  exit 1
fi

sh "$root/v3/tooling/cheng_v3.sh" print-elf \
  --object:"$obj_path" \
  --report-out:"$elf_report" >/dev/null

grep -q '^v3_elf_object_v1$' "$elf_report"
grep -q '^section_count=' "$elf_report"
grep -Eq '^section\[[0-9]+\]=' "$elf_report"
grep -q '^symbol_count=' "$elf_report"
grep -q '^reloc_count=' "$elf_report"

echo "v3 debug tools: ok"
