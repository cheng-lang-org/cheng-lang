#!/bin/bash
# CSG v2 Phase 0/1 roundtrip guard.
# Locks: Cheng writer -> canonical facts -> cold reader -> object writer.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

COLD="${CHENG_COLD:-/tmp/cheng_cold}"
DRIVER="${CHENG_BACKEND_DRIVER:-artifacts/backend_driver/cheng}"
TARGET="${CHENG_CSG_V2_TARGET:-arm64-apple-darwin}"
WORK="${CHENG_CSG_V2_WORK:-/tmp/cheng_csg_v2_roundtrip}"

if [ ! -x "$COLD" ]; then
    cc -std=c11 -O2 -o "$COLD" bootstrap/cheng_cold.c
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

    local magic8
    local magic13
    magic8="$(head -c 8 "$facts")"
    magic13="$(head -c 13 "$facts")"
    if [ "$magic13" = "CHENG_CSG_V2" ] || [ "$magic8" = "CHENGCSG" ]; then
        ok "$name csg_v2_magic"
    else
        bad "$name csg_v2_magic"
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

echo "=== CSG v2 Roundtrip ==="
roundtrip_fixture ordinary src/tests/ordinary_zero_exit_fixture.cheng 32768
roundtrip_fixture return_let tests/cheng/backend/fixtures/return_let.cheng 32768

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


# Built-in linker: ELF .o should also produce .linked executable
echo "  - builtin_linker_elf"
"$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link.o" \
  > "$WORK/ordinary_link.report.txt" 2>&1 || true
if [ -f "$WORK/ordinary_link.o.linked" ]; then
  file "$WORK/ordinary_link.o.linked" | grep -q "ELF.*executable" && echo "PASS builtin_linker_elf" || echo "FAIL builtin_linker_elf"
  pass=$((pass + 1))
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

echo "=== $pass passed, $fail failed ==="
if [ "$fail" -ne 0 ]; then
    exit 1
fi
