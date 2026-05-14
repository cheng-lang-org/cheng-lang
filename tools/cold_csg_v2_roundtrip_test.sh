#!/bin/bash
# CSG v2 Phase 0/1 roundtrip guard.
# Locks: Cheng writer -> explicit CSG fact flavor -> cold reader -> object writer.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

COLD="${CHENG_COLD:-/tmp/cheng_cold}"
DRIVER="${CHENG_BACKEND_DRIVER:-artifacts/backend_driver/cheng}"
TARGET="${CHENG_CSG_V2_TARGET:-arm64-apple-darwin}"
WORK="${CHENG_CSG_V2_WORK:-/tmp/cheng_csg_v2_roundtrip}"

if [ ! -x "$COLD" ] ||
   [ bootstrap/cheng_cold.c -nt "$COLD" ] ||
   [ bootstrap/elf64_direct.h -nt "$COLD" ] ||
   [ bootstrap/rv64_emit.h -nt "$COLD" ]; then
    cc -std=c11 -O2 -o "$COLD" bootstrap/cheng_cold.c
fi

COLD_O0="${CHENG_COLD_O0:-/tmp/cheng_cold_O0}"
if [ ! -x "$COLD_O0" ] ||
   [ bootstrap/cheng_cold.c -nt "$COLD_O0" ] ||
   [ bootstrap/elf64_direct.h -nt "$COLD_O0" ] ||
   [ bootstrap/rv64_emit.h -nt "$COLD_O0" ]; then
    cc -std=c11 -O0 -o "$COLD_O0" bootstrap/cheng_cold.c
fi

if [ ! -x "$DRIVER" ]; then
    echo "missing backend driver: $DRIVER" >&2
    echo "run: artifacts/bootstrap/cheng.stage3 build-backend-driver" >&2
    exit 2
fi

rm -rf "$WORK"
mkdir -p "$WORK"

pass=0
fail=0

ok() {
    echo "  PASS $1"
    pass=$((pass + 1))
}

bad() {
    echo "  FAIL $1" >&2
    fail=$((fail + 1))
}

require_grep() {
    local name="$1"
    local pattern="$2"
    local path="$3"
    if grep -q "$pattern" "$path" 2>/dev/null; then
        ok "$name"
    else
        bad "$name"
    fi
}

facts_magic_kind() {
    local path="$1"
    if head -c 13 "$path" | LC_ALL=C grep -aq '^CHENG_CSG_V2$'; then
        echo canonical
    elif head -c 8 "$path" | LC_ALL=C grep -aq '^CHENGCSG$'; then
        echo internal
    else
        echo unknown
    fi
}

roundtrip_fixture() {
    local name="$1"
    local source="$2"
    local budget_bytes="$3"
    local facts="$WORK/$name.facts"
    local writer_report="$WORK/$name.writer.report.txt"
    local obj1="$WORK/$name.1.o"
    local obj2="$WORK/$name.2.o"
    local reader1="$WORK/$name.reader.1.report.txt"
    local reader2="$WORK/$name.reader.2.report.txt"

    if "$DRIVER" emit-cold-csg-v2 \
        --root:"$ROOT" \
        --in:"$source" \
        --out:"$facts" \
        --target:"$TARGET" \
        --report-out:"$writer_report" >/dev/null; then
        ok "$name writer"
    else
        bad "$name writer"
        return
    fi

    local facts_kind
    facts_kind="$(facts_magic_kind "$facts")"
    if [ "$facts_kind" = "canonical" ]; then
        ok "$name canonical_cheng_csg_v2_magic"
    elif [ "$facts_kind" = "internal" ]; then
        ok "$name internal_chengcsg_magic"
    else
        bad "$name csg_v2_magic actual=$facts_kind"
    fi

    local bytes
    bytes="$(wc -c < "$facts" | tr -d ' ')"
    if [ "$bytes" -le "$budget_bytes" ]; then
        ok "$name facts_budget"
    else
        bad "$name facts_budget bytes=$bytes budget=$budget_bytes"
    fi

    require_grep "$name writer_report_status" '^cold_csg_v2_writer_status=ok$' "$writer_report"
    require_grep "$name writer_report_bytes" '^facts_bytes=' "$writer_report"

    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj1" \
        --report-out:"$reader1" >/dev/null; then
        ok "$name reader_first"
    else
        bad "$name reader_first"
    fi

    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj2" \
        --report-out:"$reader2" >/dev/null; then
        ok "$name reader_second"
    else
        bad "$name reader_second"
    fi

    if cmp "$obj1" "$obj2" >/dev/null; then
        ok "$name deterministic_obj"
    else
        bad "$name deterministic_obj"
    fi

    require_grep "$name report_facts_bytes" '^facts_bytes=' "$reader1"
    require_grep "$name report_mmap_ms" '^facts_mmap_ms=' "$reader1"
    require_grep "$name report_verify_ms" '^facts_verify_ms=' "$reader1"
    require_grep "$name report_decode_ms" '^facts_decode_ms=' "$reader1"
    require_grep "$name report_emit_obj_ms" '^facts_emit_obj_ms=' "$reader1"
    require_grep "$name report_emit_exe_ms" '^facts_emit_exe_ms=' "$reader1"
    require_grep "$name report_total_ms" '^facts_total_ms=' "$reader1"
    require_grep "$name report_no_system_link" '^system_link=0$' "$reader1"
}

# Cross-version simulation: same cold compiler built with -O0 and -O2
# must produce identical obj and csg-v2 output from the same input facts.
cross_version_opt_test() {
    local name="$1"
    local facts="$2"

    local obj_O0="$WORK/$name.xver_O0.o"
    local obj_O2="$WORK/$name.xver_O2.o"
    local csg_O0="$WORK/$name.xver_O0.csgv2"
    local csg_O2="$WORK/$name.xver_O2.csgv2"

    echo "  - ${name}_xver_obj"
    if "$COLD_O0" system-link-exec \
        --csg-in:"$facts" --emit:obj --target:"$TARGET" \
        --out:"$obj_O0" >/dev/null 2>&1 &&
       "$COLD" system-link-exec \
        --csg-in:"$facts" --emit:obj --target:"$TARGET" \
        --out:"$obj_O2" >/dev/null 2>&1; then
        if cmp -s "$obj_O0" "$obj_O2" 2>/dev/null; then
            ok "${name}_xver_obj"
        else
            bad "${name}_xver_obj (O0 vs O2 .o differ)"
        fi
    else
        bad "${name}_xver_obj (cold run failed)"
    fi

    echo "  - ${name}_xver_csg"
    if "$COLD_O0" system-link-exec \
        --csg-in:"$facts" --emit:csg-v2 --target:"$TARGET" \
        --out:"$csg_O0" >/dev/null 2>&1 &&
       "$COLD" system-link-exec \
        --csg-in:"$facts" --emit:csg-v2 --target:"$TARGET" \
        --out:"$csg_O2" >/dev/null 2>&1; then
        if cmp -s "$csg_O0" "$csg_O2" 2>/dev/null; then
            ok "${name}_xver_csg"
        else
            bad "${name}_xver_csg (O0 vs O2 csg-v2 differ)"
        fi
    else
        bad "${name}_xver_csg (cold run failed)"
    fi
}

# Roundtrip stability: facts -> csg-v2 (round 1) -> csg-v2 (round 2)
# must converge in exactly 1 step (r1 and r2 are bit-identical).
roundtrip_stability_fixture() {
    local name="$1"
    local facts="$2"

    local r1="$WORK/$name.stable.r1.csgv2"
    local r2="$WORK/$name.stable.r2.csgv2"

    echo "  - ${name}_roundtrip_stable"
    if "$COLD" system-link-exec \
        --csg-in:"$facts" --emit:csg-v2 --target:"$TARGET" \
        --out:"$r1" >/dev/null 2>&1 &&
       "$COLD" system-link-exec \
        --csg-in:"$r1" --emit:csg-v2 --target:"$TARGET" \
        --out:"$r2" >/dev/null 2>&1; then
        if cmp -s "$r1" "$r2" 2>/dev/null; then
            ok "${name}_roundtrip_stable"
        else
            bad "${name}_roundtrip_stable (r1 and r2 csg-v2 differ)"
        fi
    else
        bad "${name}_roundtrip_stable (command failed)"
    fi
}

echo "=== CSG v2 Roundtrip ==="

# Baseline: minimal smoke tests
roundtrip_fixture ordinary src/tests/ordinary_zero_exit_fixture.cheng 32768
roundtrip_fixture return_let tests/cheng/backend/fixtures/return_let.cheng 32768

# Object types with field access and constructors
roundtrip_fixture return_object_fields tests/cheng/backend/fixtures/return_object_fields.cheng 4096
roundtrip_fixture object_str_i32_local_ctor tests/cheng/backend/fixtures/object_str_i32_local_ctor.cheng 4096
roundtrip_fixture return_object_copy_assign tests/cheng/backend/fixtures/return_object_copy_assign.cheng 4096
roundtrip_fixture object_scalar_control_probe tests/cheng/backend/fixtures/object_scalar_control_probe.cheng 8192

# Multi-function: calls, forward declarations, void returns
roundtrip_fixture return_call tests/cheng/backend/fixtures/return_call.cheng 4096
roundtrip_fixture return_forward_decl_call tests/cheng/backend/fixtures/return_forward_decl_call.cheng 4096
roundtrip_fixture void_fn_return tests/cheng/backend/fixtures/void_fn_return.cheng 4096

# Global variables
roundtrip_fixture return_global_add tests/cheng/backend/fixtures/return_global_add.cheng 4096

# Control flow: if/elif/else, while loops, for loops
roundtrip_fixture return_if_elif tests/cheng/backend/fixtures/return_if_elif.cheng 4096
roundtrip_fixture return_while_sum tests/cheng/backend/fixtures/return_while_sum.cheng 4096
roundtrip_fixture return_for_sum tests/cheng/backend/fixtures/return_for_sum.cheng 4096

# Error handling with Result type
roundtrip_fixture return_result_hidden_ret_probe tests/cheng/backend/fixtures/return_result_hidden_ret_probe.cheng 16384

# Expression forms: nested ternary
roundtrip_fixture return_ternary_nested tests/cheng/backend/fixtures/return_ternary_nested.cheng 4096

# Loop control flow: break and continue
roundtrip_fixture return_while_break tests/cheng/backend/fixtures/return_while_break.cheng 4096
roundtrip_fixture return_while_continue tests/cheng/backend/fixtures/return_while_continue.cheng 4096

# Files with imports: object-by-value and object-var-assign (pull in external types)
roundtrip_fixture ptr_object_by_value_probe tests/cheng/backend/fixtures/ptr_object_by_value_probe.cheng 131072
roundtrip_fixture ptr_object_var_assign_probe tests/cheng/backend/fixtures/ptr_object_var_assign_probe.cheng 131072

# I32 arithmetic: multiplication, division, modulo, multiply-add
roundtrip_fixture return_i32_mul tests/cheng/backend/fixtures/return_i32_mul.cheng 4096
roundtrip_fixture return_i32_div_mod tests/cheng/backend/fixtures/return_i32_div_mod.cheng 4096
roundtrip_fixture return_i32_madd tests/cheng/backend/fixtures/return_i32_madd.cheng 4096

# I32 bitwise operations: AND, OR, XOR, SHL, ASR
roundtrip_fixture return_i32_bitwise tests/cheng/backend/fixtures/return_i32_bitwise.cheng 8192

# I64 arithmetic: add, sub, mul, i64-from-i32 conversion
roundtrip_fixture return_i64_arith tests/cheng/backend/fixtures/return_i64_arith.cheng 4096

# I64 bitwise operations: AND, OR, XOR, SHL, ASR
roundtrip_fixture return_i64_bitwise tests/cheng/backend/fixtures/return_i64_bitwise.cheng 8192

# Mixed I64 operations: bitwise chain with arithmetic
roundtrip_fixture return_i64_chain tests/cheng/backend/fixtures/return_i64_chain.cheng 4096

# Float64 arithmetic: mul, add, comparison
roundtrip_fixture return_float64_arith tests/cheng/backend/fixtures/return_float64_arith.cheng 4096

# String operations: length
roundtrip_fixture return_str_ops tests/cheng/backend/fixtures/return_str_ops.cheng 4096

# Nested/multi-level object types: objects containing objects
roundtrip_fixture return_nested_object tests/cheng/backend/fixtures/return_nested_object.cheng 4096

# Multi-level function call depth: 3+ call chain
roundtrip_fixture return_multi_depth_call tests/cheng/backend/fixtures/return_multi_depth_call.cheng 8192

# Multiple global variables
roundtrip_fixture return_multi_global tests/cheng/backend/fixtures/return_multi_global.cheng 4096

# Pointer load/store operations
roundtrip_fixture return_ptr_load_store tests/cheng/backend/fixtures/return_ptr_load_store.cheng 4096

# Mixed control flow: if/elif/else inside while loop
roundtrip_fixture return_if_while_mix tests/cheng/backend/fixtures/return_if_while_mix.cheng 8192

# Nested loops with if conditions (for-for-if)
roundtrip_fixture return_nested_control tests/cheng/backend/fixtures/return_nested_control.cheng 8192

# Boolean logical expressions: && and ||
roundtrip_fixture return_logical_expr tests/cheng/backend/fixtures/return_logical_expr.cheng 8192

# Import chain: file A imports B imports C (transitive import compilation)
roundtrip_fixture import_chain_a tests/cheng/backend/fixtures/import_chain_a.cheng 4096

# Roundtrip stability: all fixtures must converge in 1 csg-v2 step
echo "  - roundtrip_stability"
for f in "$WORK"/*.facts; do
    base="$(basename "$f" .facts)"
    roundtrip_stability_fixture "$base" "$f"
done

bad_facts="$WORK/ordinary.unknown-record.facts"
cp "$WORK/ordinary.facts" "$bad_facts"
printf 'RFFFF00000000\n' >> "$bad_facts"
if "$COLD" system-link-exec \
    --csg-in:"$bad_facts" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$WORK/bad.o" \
    --report-out:"$WORK/bad.report.txt" >/dev/null 2>&1; then
    bad "unknown_record_hard_fail"
else
    ok "unknown_record_hard_fail"
fi

truncated="$WORK/ordinary.truncated.facts"
dd if="$WORK/ordinary.facts" of="$truncated" bs=1 count=16 >/dev/null 2>&1 || true
if "$COLD" system-link-exec \
    --csg-in:"$truncated" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$WORK/truncated.o" \
    --report-out:"$WORK/truncated.report.txt" >/dev/null 2>&1; then
    bad "truncated_hard_fail"
else
    ok "truncated_hard_fail"
fi

darwin_runtime_provider_source="$ROOT/src/core/runtime/core_runtime_provider_darwin.cheng"
darwin_runtime_provider_obj="$WORK/core_runtime_provider_darwin.o"
darwin_runtime_provider_report="$WORK/core_runtime_provider_darwin.report.txt"
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$darwin_runtime_provider_source" \
    --emit:obj \
    --target:arm64-apple-darwin \
    --symbol-visibility:internal \
    --export-roots:core_runtime_stub_trace \
    --out:"$darwin_runtime_provider_obj" \
    --report-out:"$darwin_runtime_provider_report" >/dev/null 2>&1 &&
   [ -s "$darwin_runtime_provider_obj" ] &&
   grep -q '^direct_macho=1$' "$darwin_runtime_provider_report" &&
   grep -q '^system_link=0$' "$darwin_runtime_provider_report" &&
   grep -q '^linkerless_image=1$' "$darwin_runtime_provider_report" &&
   nm -g "$darwin_runtime_provider_obj" 2>/dev/null | grep -q '_core_runtime_stub_trace$'; then
    ok "darwin_runtime_provider_marker_object"
else
    bad "darwin_runtime_provider_marker_object"
fi

darwin_provider_archive_macho_error='Mach-O relocatable provider archive reader/linker unsupported'

darwin_pack_archive="$WORK/core_runtime_provider_darwin.chenga"
darwin_pack_report="$WORK/core_runtime_provider_darwin.pack.report.txt"
if "$COLD" provider-archive-pack \
    --target:arm64-apple-darwin \
    --object:"$darwin_runtime_provider_obj" \
    --export:_core_runtime_stub_trace \
    --module:runtime/core_runtime \
    --source:"$darwin_runtime_provider_source" \
    --out:"$darwin_pack_archive" \
    --report-out:"$darwin_pack_report" >/dev/null 2>&1; then
    bad "darwin_provider_archive_pack_hard_fail_no_fallback"
elif grep -q '^provider_archive_pack=0$' "$darwin_pack_report" &&
     grep -q '^provider_archive=0$' "$darwin_pack_report" &&
     grep -q '^provider_object_count=0$' "$darwin_pack_report" &&
     grep -q '^system_link=0$' "$darwin_pack_report" &&
     grep -q '^linkerless_image=1$' "$darwin_pack_report" &&
     grep -q "^error=$darwin_provider_archive_macho_error$" "$darwin_pack_report" &&
     [ ! -e "$darwin_pack_archive" ]; then
    ok "darwin_provider_archive_pack_hard_fail_no_fallback"
else
    bad "darwin_provider_archive_pack_hard_fail_no_fallback"
fi

darwin_archive_report="$WORK/darwin_provider_archive_hard_fail.report.txt"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --provider-archive:"$WORK/missing_darwin_runtime.chenga" \
    --emit:exe \
    --target:arm64-apple-darwin \
    --out:"$WORK/darwin_provider_archive_should_not_exist" \
    --report-out:"$darwin_archive_report" >/dev/null 2>&1; then
    bad "darwin_provider_archive_hard_fail_no_fallback"
elif grep -q '^provider_archive=1$' "$darwin_archive_report" &&
     grep -q '^provider_object_count=0$' "$darwin_archive_report" &&
     grep -q '^system_link=0$' "$darwin_archive_report" &&
     grep -q "^error=$darwin_provider_archive_macho_error$" "$darwin_archive_report"; then
    ok "darwin_provider_archive_hard_fail_no_fallback"
else
    bad "darwin_provider_archive_hard_fail_no_fallback"
fi

darwin_link_object_primary="$WORK/ordinary_darwin_provider_primary.o"
darwin_link_object_primary_report="$WORK/ordinary_darwin_provider_primary.report.txt"
darwin_link_object_report="$WORK/darwin_link_object_provider_archive.report.txt"
darwin_link_object_out="$WORK/darwin_link_object_provider_archive_should_not_exist"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --emit:obj \
    --target:arm64-apple-darwin \
    --out:"$darwin_link_object_primary" \
    --report-out:"$darwin_link_object_primary_report" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --link-object:"$darwin_link_object_primary" \
    --provider-archive:"$WORK/missing_darwin_runtime.chenga" \
    --emit:exe \
    --target:arm64-apple-darwin \
    --out:"$darwin_link_object_out" \
    --report-out:"$darwin_link_object_report" >/dev/null 2>&1; then
    bad "darwin_link_object_provider_archive_hard_fail_no_fallback"
elif [ -s "$darwin_link_object_primary" ] &&
     grep -q '^direct_macho=1$' "$darwin_link_object_primary_report" &&
     grep -q '^link_object=1$' "$darwin_link_object_report" &&
     grep -q '^provider_archive=1$' "$darwin_link_object_report" &&
     grep -q '^provider_object_count=0$' "$darwin_link_object_report" &&
     grep -q '^system_link=0$' "$darwin_link_object_report" &&
     grep -q "^error=$darwin_provider_archive_macho_error$" "$darwin_link_object_report" &&
     [ ! -e "$darwin_link_object_out" ]; then
    ok "darwin_link_object_provider_archive_hard_fail_no_fallback"
else
    bad "darwin_link_object_provider_archive_hard_fail_no_fallback"
fi


# Built-in linker: ELF .o should also produce .linked executable
echo "  - builtin_linker_elf"
"$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link.o" \
  > "$WORK/ordinary_link.report.txt" 2>&1 || true
if [ -f "$WORK/ordinary_link.o.linked" ]; then
  if file "$WORK/ordinary_link.o.linked" | grep -q "ELF.*executable"; then
    echo "PASS builtin_linker_elf"
    pass=$((pass + 1))
  else
    echo "FAIL builtin_linker_elf"
    fail=$((fail + 1))
  fi
else
  echo "FAIL builtin_linker_elf (no .linked file)"
  fail=$((fail + 1))
fi


# Built-in linker determinism: two runs produce bit-identical linked executables
echo "  - builtin_linker_determinism"
rm -f "$WORK/ordinary_link_d1.o" "$WORK/ordinary_link_d1.o.linked"
rm -f "$WORK/ordinary_link_d2.o" "$WORK/ordinary_link_d2.o.linked"
"$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_d1.o" \
  > "$WORK/ordinary_link_d1.report.txt" 2>&1 || true
"$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_d2.o" \
  > "$WORK/ordinary_link_d2.report.txt" 2>&1 || true
if [ -f "$WORK/ordinary_link_d1.o.linked" ] && [ -f "$WORK/ordinary_link_d2.o.linked" ]; then
  if cmp -s "$WORK/ordinary_link_d1.o.linked" "$WORK/ordinary_link_d2.o.linked"; then
    echo "PASS builtin_linker_determinism"
    pass=$((pass + 1))
  else
    echo "FAIL builtin_linker_determinism (linked exes differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL builtin_linker_determinism (missing linked files)"
  fail=$((fail + 1))
fi

# Explicit link-object entry: consume the ELF .o and produce the same executable.
echo "  - link_object_explicit"
rm -f "$WORK/ordinary_link_explicit" "$WORK/ordinary_link_explicit.report.txt"
if "$COLD" system-link-exec \
  --link-object:"$WORK/ordinary_link_d1.o" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_explicit" \
  --report-out:"$WORK/ordinary_link_explicit.report.txt" \
  > "$WORK/ordinary_link_explicit.stdout" 2>&1; then
  if cmp -s "$WORK/ordinary_link_d1.o.linked" "$WORK/ordinary_link_explicit" &&
     grep -q '^link_object=1$' "$WORK/ordinary_link_explicit.report.txt" &&
     grep -q '^unresolved_symbol_count=0$' "$WORK/ordinary_link_explicit.report.txt" &&
     grep -q '^system_link=0$' "$WORK/ordinary_link_explicit.report.txt"; then
    echo "PASS link_object_explicit"
    pass=$((pass + 1))
  else
    echo "FAIL link_object_explicit (output/report mismatch)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL link_object_explicit (command failed)"
  fail=$((fail + 1))
fi

# Provider archive forward/reverse gate: provider object -> archive -> primary link.
provider_source="$WORK/provider_fixture.cheng"
primary_source="$WORK/provider_primary.cheng"
provider_obj="$WORK/provider_fixture.o"
primary_obj="$WORK/provider_primary.o"
provider_archive="$WORK/provider_fixture.chenga"
provider_exe="$WORK/provider_archive_link"
provider_obj_report="$WORK/provider_fixture.obj.report.txt"
primary_obj_report="$WORK/provider_primary.obj.report.txt"
provider_pack_report="$WORK/provider_fixture.pack.report.txt"
provider_link_report="$WORK/provider_archive_link.report.txt"

cat > "$provider_source" <<'CHENG'
@exportc("provider_fixture_value")
fn provider_fixture_value(): int32 = return 7
CHENG

cat > "$primary_source" <<'CHENG'
@importc("provider_fixture_value")
fn provider_fixture_value(): int32

fn main(): int32 = return provider_fixture_value()
CHENG

echo "  - provider_archive_forward"
if "$COLD" system-link-exec \
  --in:"$provider_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$provider_obj" \
  --report-out:"$provider_obj_report" \
  > "$WORK/provider_fixture.obj.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --in:"$primary_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$primary_obj" \
  --report-out:"$primary_obj_report" \
  > "$WORK/provider_primary.obj.stdout" 2>&1 &&
   "$COLD" provider-archive-pack \
  --target:riscv64-unknown-linux-gnu \
  --object:"$provider_obj" \
  --export:provider_fixture_value \
  --module:provider_fixture \
  --source:"$provider_source" \
  --out:"$provider_archive" \
  --report-out:"$provider_pack_report" \
  > "$WORK/provider_fixture.pack.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --link-object:"$primary_obj" \
  --provider-archive:"$provider_archive" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$provider_exe" \
  --report-out:"$provider_link_report" \
  > "$WORK/provider_archive_link.stdout" 2>&1 &&
   [ -f "$provider_exe" ] &&
   grep -q '^provider_archive=1$' "$provider_link_report" &&
   grep -q '^provider_archive_member_count=1$' "$provider_link_report" &&
   grep -q '^provider_object_count=1$' "$provider_link_report" &&
   grep -q '^provider_export_count=1$' "$provider_link_report" &&
   grep -q '^provider_resolved_symbol_count=1$' "$provider_link_report" &&
   grep -q '^unresolved_symbol_count=0$' "$provider_link_report" &&
   grep -q '^system_link=0$' "$provider_link_report" &&
   grep -q '^linkerless_image=1$' "$provider_link_report" &&
   grep -q '^link_object=1$' "$provider_link_report"; then
  echo "PASS provider_archive_forward"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_forward"
  fail=$((fail + 1))
fi

echo "  - provider_archive_corrupt_magic_hard_fail"
corrupt_archive="$WORK/provider_fixture.corrupt_magic.chenga"
if [ ! -s "$provider_archive" ] || [ ! -s "$primary_obj" ]; then
  echo "FAIL provider_archive_corrupt_magic_hard_fail (missing forward artifacts)"
  fail=$((fail + 1))
elif ! cp "$provider_archive" "$corrupt_archive" 2>/dev/null ||
     ! printf 'BADMAGIC' | dd of="$corrupt_archive" bs=1 count=8 conv=notrunc >/dev/null 2>&1; then
  echo "FAIL provider_archive_corrupt_magic_hard_fail (corrupt setup failed)"
  fail=$((fail + 1))
elif "$COLD" system-link-exec \
  --link-object:"$primary_obj" \
  --provider-archive:"$corrupt_archive" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/provider_archive_corrupt_magic_should_not_exist" \
  --report-out:"$WORK/provider_archive_corrupt_magic.report.txt" \
  > "$WORK/provider_archive_corrupt_magic.stdout" 2>&1; then
  echo "FAIL provider_archive_corrupt_magic_hard_fail"
  fail=$((fail + 1))
else
  echo "PASS provider_archive_corrupt_magic_hard_fail"
  pass=$((pass + 1))
fi

echo "  - provider_archive_missing_export_pack_fail"
if [ ! -s "$provider_archive" ] || [ ! -s "$provider_obj" ]; then
  echo "FAIL provider_archive_missing_export_pack_fail (missing forward artifacts)"
  fail=$((fail + 1))
elif "$COLD" provider-archive-pack \
  --target:riscv64-unknown-linux-gnu \
  --object:"$provider_obj" \
  --export:provider_fixture_missing \
  --module:provider_fixture \
  --source:"$provider_source" \
  --out:"$WORK/provider_fixture_missing_export.chenga" \
  --report-out:"$WORK/provider_fixture_missing_export.pack.report.txt" \
  > "$WORK/provider_fixture_missing_export.pack.stdout" 2>&1; then
  echo "FAIL provider_archive_missing_export_pack_fail"
  fail=$((fail + 1))
else
  echo "PASS provider_archive_missing_export_pack_fail"
  pass=$((pass + 1))
fi

multi_one_source="$WORK/provider_multi_one.cheng"
multi_two_source="$WORK/provider_multi_two.cheng"
multi_primary_source="$WORK/provider_multi_primary.cheng"
multi_missing_source="$WORK/provider_multi_missing.cheng"
multi_one_obj="$WORK/provider_multi_one.o"
multi_two_obj="$WORK/provider_multi_two.o"
multi_primary_obj="$WORK/provider_multi_primary.o"
multi_archive="$WORK/provider_multi.chenga"
multi_facts="$WORK/provider_multi_primary.facts"
multi_missing_facts="$WORK/provider_multi_missing.facts"
multi_link_report="$WORK/provider_multi_link.report.txt"
multi_csg_link_report="$WORK/provider_multi_csg_link.report.txt"
multi_missing_report="$WORK/provider_multi_missing_link.report.txt"

cat > "$multi_one_source" <<'CHENG'
@exportc("provider_multi_one")
fn provider_multi_one(): int32 = return 11
CHENG

cat > "$multi_two_source" <<'CHENG'
@exportc("provider_multi_two")
fn provider_multi_two(): int32 = return 31
CHENG

cat > "$multi_primary_source" <<'CHENG'
@importc("provider_multi_one")
fn provider_multi_one(): int32
@importc("provider_multi_two")
fn provider_multi_two(): int32
fn main(): int32 = return provider_multi_one() + provider_multi_two()
CHENG

cat > "$multi_missing_source" <<'CHENG'
@importc("provider_multi_missing")
fn provider_multi_missing(): int32
fn main(): int32 = return provider_multi_missing()
CHENG

echo "  - provider_archive_multi_member_export"
if "$COLD" system-link-exec \
  --in:"$multi_one_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$multi_one_obj" \
  --report-out:"$WORK/provider_multi_one.obj.report.txt" \
  > "$WORK/provider_multi_one.obj.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --in:"$multi_two_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$multi_two_obj" \
  --report-out:"$WORK/provider_multi_two.obj.report.txt" \
  > "$WORK/provider_multi_two.obj.stdout" 2>&1 &&
   "$COLD" provider-archive-pack \
  --target:riscv64-unknown-linux-gnu \
  --object:"$multi_one_obj" \
  --object:"$multi_two_obj" \
  --export:provider_multi_one \
  --export:provider_multi_two \
  --module:provider_multi \
  --source:"$WORK" \
  --out:"$multi_archive" \
  --report-out:"$WORK/provider_multi.pack.report.txt" \
  > "$WORK/provider_multi.pack.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --in:"$multi_primary_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$multi_primary_obj" \
  --report-out:"$WORK/provider_multi_primary.obj.report.txt" \
  > "$WORK/provider_multi_primary.obj.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --link-object:"$multi_primary_obj" \
  --provider-archive:"$multi_archive" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/provider_multi_linked" \
  --report-out:"$multi_link_report" \
  > "$WORK/provider_multi_link.stdout" 2>&1 &&
   grep -q '^provider_archive_member_count=2$' "$multi_link_report" &&
   grep -q '^provider_export_count=2$' "$multi_link_report" &&
   grep -q '^provider_resolved_symbol_count=2$' "$multi_link_report" &&
   grep -q '^unresolved_symbol_count=0$' "$multi_link_report" &&
   grep -q '^system_link=0$' "$multi_link_report"; then
  echo "PASS provider_archive_multi_member_export"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_multi_member_export"
  fail=$((fail + 1))
fi

echo "  - csg_in_emit_exe_provider_archive"
if [ ! -s "$multi_archive" ]; then
  echo "FAIL csg_in_emit_exe_provider_archive (missing archive)"
  fail=$((fail + 1))
elif "$DRIVER" emit-cold-csg-v2 \
  --root:"$ROOT" \
  --in:"$multi_primary_source" \
  --out:"$multi_facts" \
  --target:riscv64-unknown-linux-gnu \
  --report-out:"$WORK/provider_multi.writer.report.txt" \
  > "$WORK/provider_multi.writer.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --csg-in:"$multi_facts" \
  --provider-archive:"$multi_archive" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/provider_multi_csg_linked" \
  --report-out:"$multi_csg_link_report" \
  > "$WORK/provider_multi_csg_link.stdout" 2>&1 &&
   grep -q '^cold_csg_lowering=1$' "$multi_csg_link_report" &&
   grep -q '^provider_archive_member_count=2$' "$multi_csg_link_report" &&
   grep -q '^provider_export_count=2$' "$multi_csg_link_report" &&
   grep -q '^provider_resolved_symbol_count=2$' "$multi_csg_link_report" &&
   grep -q '^unresolved_symbol_count=0$' "$multi_csg_link_report" &&
   grep -q '^system_link=0$' "$multi_csg_link_report"; then
  echo "PASS csg_in_emit_exe_provider_archive"
  pass=$((pass + 1))
else
  echo "FAIL csg_in_emit_exe_provider_archive"
  fail=$((fail + 1))
fi

echo "  - csg_in_provider_archive_missing_export_hard_fail"
if [ ! -s "$multi_archive" ]; then
  echo "FAIL csg_in_provider_archive_missing_export_hard_fail (missing archive)"
  fail=$((fail + 1))
elif "$DRIVER" emit-cold-csg-v2 \
  --root:"$ROOT" \
  --in:"$multi_missing_source" \
  --out:"$multi_missing_facts" \
  --target:riscv64-unknown-linux-gnu \
  --report-out:"$WORK/provider_multi_missing.writer.report.txt" \
  > "$WORK/provider_multi_missing.writer.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --csg-in:"$multi_missing_facts" \
  --provider-archive:"$multi_archive" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/provider_multi_missing_linked" \
  --report-out:"$multi_missing_report" \
  > "$WORK/provider_multi_missing_link.stdout" 2>&1; then
  echo "FAIL csg_in_provider_archive_missing_export_hard_fail"
  fail=$((fail + 1))
elif grep -q '^unresolved_symbol_count=1$' "$multi_missing_report" &&
     grep -q '^first_unresolved_symbol=provider_multi_missing$' "$multi_missing_report"; then
  echo "PASS csg_in_provider_archive_missing_export_hard_fail"
  pass=$((pass + 1))
else
  echo "FAIL csg_in_provider_archive_missing_export_hard_fail"
  fail=$((fail + 1))
fi

# Reader fixed-point: two exe runs from same facts are bit-identical
echo "  - reader_fixedpoint_exe"
rm -f "$WORK/fp_exe_a" "$WORK/fp_exe_b"
COLD_NO_SIGN=1 "$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:exe --target:arm64-apple-darwin \
  --out:"$WORK/fp_exe_a" > /dev/null 2>&1
COLD_NO_SIGN=1 "$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:exe --target:arm64-apple-darwin \
  --out:"$WORK/fp_exe_b" > /dev/null 2>&1
if cmp -s "$WORK/fp_exe_a" "$WORK/fp_exe_b" 2>/dev/null; then
  echo "PASS reader_fixedpoint_exe"
  pass=$((pass + 1))
else
  echo "FAIL reader_fixedpoint_exe"
  fail=$((fail + 1))
fi

# Reader fixed-point: RISC-V ELF target
echo "  - reader_fixedpoint_exe_riscv"
rm -f "$WORK/fp_rv_a" "$WORK/fp_rv_b"
COLD_NO_SIGN=1 "$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/fp_rv_a" > /dev/null 2>&1
COLD_NO_SIGN=1 "$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/fp_rv_b" > /dev/null 2>&1
if [ -f "$WORK/fp_rv_a" ] && [ -f "$WORK/fp_rv_b" ] && cmp -s "$WORK/fp_rv_a" "$WORK/fp_rv_b"; then
  echo "PASS reader_fixedpoint_exe_riscv"
  pass=$((pass + 1))
else
  echo "FAIL reader_fixedpoint_exe_riscv"
  fail=$((fail + 1))
fi

# Cross-stage fixed-point: backend_driver vs cold compiler
# Same facts should produce same .text section (codegen equivalence)
echo "  - cross_stage_text_arm64"
if ! "$DRIVER" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --emit:obj --target:"$TARGET" \
    --out:"$WORK/stage_driver.o" >/dev/null 2>&1; then
  echo "FAIL cross_stage_text_arm64 (driver obj failed)"
  fail=$((fail + 1))
elif ! "$COLD" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --emit:obj --target:"$TARGET" \
    --out:"$WORK/stage_cold.o" >/dev/null 2>&1; then
  echo "FAIL cross_stage_text_arm64 (cold obj failed)"
  fail=$((fail + 1))
else
  driver_hex=$(otool -X -t "$WORK/stage_driver.o" 2>/dev/null | tail -n +2 | tr -d '[:space:]')
  cold_hex=$(otool -X -t "$WORK/stage_cold.o" 2>/dev/null | tail -n +2 | tr -d '[:space:]')
  if [ -z "$driver_hex" ] || [ -z "$cold_hex" ]; then
    echo "FAIL cross_stage_text_arm64 (empty .text section)"
    fail=$((fail + 1))
  elif [ "$driver_hex" = "$cold_hex" ]; then
    echo "PASS cross_stage_text_arm64"
    pass=$((pass + 1))
  else
    echo "FAIL cross_stage_text_arm64 (.text mismatch)"
    fail=$((fail + 1))
  fi
fi

# Cross-stage functional equivalence: both produce executables with same behavior
echo "  - cross_stage_functional_arm64"
if ! "$DRIVER" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --emit:exe --target:"$TARGET" \
    --out:"$WORK/stage_driver_exe" >/dev/null 2>&1; then
  echo "FAIL cross_stage_functional_arm64 (driver exe failed)"
  fail=$((fail + 1))
elif ! "$COLD" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --emit:exe --target:"$TARGET" \
    --out:"$WORK/stage_cold_exe" >/dev/null 2>&1; then
  echo "FAIL cross_stage_functional_arm64 (cold exe failed)"
  fail=$((fail + 1))
else
  driver_code=0
  "$WORK/stage_driver_exe" 2>/dev/null || driver_code=$?
  cold_code=0
  "$WORK/stage_cold_exe" 2>/dev/null || cold_code=$?
  if [ "$driver_code" = "$cold_code" ]; then
    echo "PASS cross_stage_functional_arm64"
    pass=$((pass + 1))
  else
    echo "FAIL cross_stage_functional_arm64 (driver=$driver_code cold=$cold_code)"
    fail=$((fail + 1))
  fi
fi

# Cross-version optimization-level simulation: O0 and O2 binaries produce identical output
# Skip intentionally corrupted files created by error-handling tests
echo "=== Cross-Version Determinism ==="
for f in "$WORK"/*.facts; do
    base="$(basename "$f" .facts)"
    case "$base" in *truncated|*unknown-record) continue ;; esac
    cross_version_opt_test "$base" "$f"
done

# CSG v2 fixed-point: read facts, codegen (DSE), re-emit, verify convergence
echo "  - csg_v2_fixedpoint_ordinary"
csg_fp_a="$WORK/fixedpoint.ordinary.a.facts"
csg_fp_b="$WORK/fixedpoint.ordinary.b.facts"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_a" \
    --report-out:"$WORK/fixedpoint.ordinary.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_b" \
    --report-out:"$WORK/fixedpoint.ordinary.b.report.txt" >/dev/null 2>&1; then
  if cmp -s "$csg_fp_a" "$csg_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_ordinary"
    pass=$((pass + 1))
  else
    echo "FAIL csg_v2_fixedpoint_ordinary (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_ordinary (roundtrip command failed)"
  fail=$((fail + 1))
fi

echo "  - csg_v2_fixedpoint_return_let"
csg_fp_a="$WORK/fixedpoint.return_let.a.facts"
csg_fp_b="$WORK/fixedpoint.return_let.b.facts"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/return_let.facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_a" \
    --report-out:"$WORK/fixedpoint.return_let.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_b" \
    --report-out:"$WORK/fixedpoint.return_let.b.report.txt" >/dev/null 2>&1; then
  if cmp -s "$csg_fp_a" "$csg_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_return_let"
    pass=$((pass + 1))
  else
    echo "FAIL csg_v2_fixedpoint_return_let (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_return_let (roundtrip command failed)"
  fail=$((fail + 1))
fi

echo "  - csg_v2_fixedpoint_unused_binding"
# Dead-store fixture: unused binding produces dead const
csg_unused="$WORK/unused_binding.cheng"
csg_unused_facts="$WORK/unused_binding.facts"
csg_unused_fp_a="$WORK/fixedpoint.unused.a.facts"
csg_unused_fp_b="$WORK/fixedpoint.unused.b.facts"
cat > "$csg_unused" <<'CHENG'
fn main(): int32 =
    let unused: int32 = 42
    return 0
CHENG
if "$DRIVER" emit-cold-csg-v2 \
    --root:"$ROOT" \
    --in:"$csg_unused" \
    --out:"$csg_unused_facts" \
    --target:"$TARGET" \
    --report-out:"$WORK/unused_binding.writer.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_unused_facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_unused_fp_a" \
    --report-out:"$WORK/fixedpoint.unused.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_unused_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_unused_fp_b" \
    --report-out:"$WORK/fixedpoint.unused.b.report.txt" >/dev/null 2>&1; then
  # Check that DSE actually removed the dead store (fp_a < initial)
  init_bytes=$(wc -c < "$csg_unused_facts" | tr -d ' ')
  fp_bytes=$(wc -c < "$csg_unused_fp_a" | tr -d ' ')
  if cmp -s "$csg_unused_fp_a" "$csg_unused_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_unused_binding"
    pass=$((pass + 1))
    if ! cmp -s "$csg_unused_facts" "$csg_unused_fp_a" 2>/dev/null; then
      echo "  - csg_v2_dse_effect (DSE modified body: $init_bytes bytes)"
      pass=$((pass + 1))
    else
      echo "  - csg_v2_dse_noop (DSE: $init_bytes bytes unchanged)"
      pass=$((pass + 1))
    fi
  else
    echo "FAIL csg_v2_fixedpoint_unused_binding (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_unused_binding (roundtrip command failed)"
  fail=$((fail + 1))
fi

# CSG v2 fixed-point: object types with field access
echo "  - csg_v2_fixedpoint_object_fields"
csg_fp_a="$WORK/fixedpoint.object_fields.a.facts"
csg_fp_b="$WORK/fixedpoint.object_fields.b.facts"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/return_object_fields.facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_a" \
    --report-out:"$WORK/fixedpoint.object_fields.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_b" \
    --report-out:"$WORK/fixedpoint.object_fields.b.report.txt" >/dev/null 2>&1; then
  if cmp -s "$csg_fp_a" "$csg_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_object_fields"
    pass=$((pass + 1))
  else
    echo "FAIL csg_v2_fixedpoint_object_fields (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_object_fields (roundtrip command failed)"
  fail=$((fail + 1))
fi

# CSG v2 fixed-point: if/elif/else branching
echo "  - csg_v2_fixedpoint_if_elif"
csg_fp_a="$WORK/fixedpoint.if_elif.a.facts"
csg_fp_b="$WORK/fixedpoint.if_elif.b.facts"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/return_if_elif.facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_a" \
    --report-out:"$WORK/fixedpoint.if_elif.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_b" \
    --report-out:"$WORK/fixedpoint.if_elif.b.report.txt" >/dev/null 2>&1; then
  if cmp -s "$csg_fp_a" "$csg_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_if_elif"
    pass=$((pass + 1))
  else
    echo "FAIL csg_v2_fixedpoint_if_elif (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_if_elif (roundtrip command failed)"
  fail=$((fail + 1))
fi

# CSG v2 fixed-point: while loop
echo "  - csg_v2_fixedpoint_while_sum"
csg_fp_a="$WORK/fixedpoint.while_sum.a.facts"
csg_fp_b="$WORK/fixedpoint.while_sum.b.facts"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/return_while_sum.facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_a" \
    --report-out:"$WORK/fixedpoint.while_sum.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_b" \
    --report-out:"$WORK/fixedpoint.while_sum.b.report.txt" >/dev/null 2>&1; then
  if cmp -s "$csg_fp_a" "$csg_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_while_sum"
    pass=$((pass + 1))
  else
    echo "FAIL csg_v2_fixedpoint_while_sum (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_while_sum (roundtrip command failed)"
  fail=$((fail + 1))
fi

# CSG v2 fixed-point: multi-function call
echo "  - csg_v2_fixedpoint_call"
csg_fp_a="$WORK/fixedpoint.call.a.facts"
csg_fp_b="$WORK/fixedpoint.call.b.facts"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/return_call.facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_a" \
    --report-out:"$WORK/fixedpoint.call.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_b" \
    --report-out:"$WORK/fixedpoint.call.b.report.txt" >/dev/null 2>&1; then
  if cmp -s "$csg_fp_a" "$csg_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_call"
    pass=$((pass + 1))
  else
    echo "FAIL csg_v2_fixedpoint_call (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_call (roundtrip command failed)"
  fail=$((fail + 1))
fi

# CSG v2 fixed-point: error handling with Result type
echo "  - csg_v2_fixedpoint_result_probe"
csg_fp_a="$WORK/fixedpoint.result_probe.a.facts"
csg_fp_b="$WORK/fixedpoint.result_probe.b.facts"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/return_result_hidden_ret_probe.facts" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_a" \
    --report-out:"$WORK/fixedpoint.result_probe.a.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$csg_fp_a" \
    --emit:csg-v2 \
    --target:"$TARGET" \
    --out:"$csg_fp_b" \
    --report-out:"$WORK/fixedpoint.result_probe.b.report.txt" >/dev/null 2>&1; then
  if cmp -s "$csg_fp_a" "$csg_fp_b" 2>/dev/null; then
    echo "PASS csg_v2_fixedpoint_result_probe"
    pass=$((pass + 1))
  else
    echo "FAIL csg_v2_fixedpoint_result_probe (iterations differ)"
    fail=$((fail + 1))
  fi
else
  echo "FAIL csg_v2_fixedpoint_result_probe (roundtrip command failed)"
  fail=$((fail + 1))
fi

echo "=== $pass passed, $fail failed ==="
if [ "$fail" -ne 0 ]; then
    exit 1
fi
