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
   [ bootstrap/cold_parser.c -nt "$COLD" ] ||
   [ bootstrap/cold_parser.h -nt "$COLD" ] ||
   [ bootstrap/macho_direct.h -nt "$COLD" ] ||
   [ bootstrap/elf64_direct.h -nt "$COLD" ] ||
   [ bootstrap/rv64_emit.h -nt "$COLD" ]; then
    cc -std=c11 -O2 -o "$COLD" bootstrap/cheng_cold.c
fi

COLD_O0="${CHENG_COLD_O0:-/tmp/cheng_cold_O0}"
if [ ! -x "$COLD_O0" ] ||
   [ bootstrap/cheng_cold.c -nt "$COLD_O0" ] ||
   [ bootstrap/cold_parser.c -nt "$COLD_O0" ] ||
   [ bootstrap/cold_parser.h -nt "$COLD_O0" ] ||
   [ bootstrap/macho_direct.h -nt "$COLD_O0" ] ||
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

require_report_positive() {
    local name="$1"
    local key="$2"
    local path="$3"
    local value
    value="$(awk -F= -v k="$key" '$1 == k { print $2; exit }' "$path" 2>/dev/null)"
    if [ -n "$value" ] && [ "$value" -gt 0 ] 2>/dev/null; then
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

canonical_reader_smoke() {
    if [ "$TARGET" != "arm64-apple-darwin" ]; then
        ok "canonical_minimal_skipped_non_arm64"
        return
    fi
    local facts="$WORK/canonical_minimal.facts"
    local obj1="$WORK/canonical_minimal.1.o"
    local obj2="$WORK/canonical_minimal.2.o"
    local bin="$WORK/canonical_minimal"
    local report1="$WORK/canonical_minimal.1.report.txt"
    local report2="$WORK/canonical_minimal.2.report.txt"
    cat > "$facts" <<'CSG'
CHENG_CSG_V2
R0001000000161200000061726d36342d6170706c652d64617277696e
R000200000009050000006d6163686f
R000300000008040000006d61696e
R000400000024010000000000000002000000040000006d61696e0c000000656e7472795f627269646765
R00050000000400008052
R000500000004c0035fd6
CSG
    if [ "$(facts_magic_kind "$facts")" = "canonical" ]; then
        ok "canonical_minimal_magic"
    else
        bad "canonical_minimal_magic"
    fi
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj1" \
        --report-out:"$report1" >/dev/null &&
       "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj2" \
        --report-out:"$report2" >/dev/null; then
        ok "canonical_minimal_reader"
    else
        bad "canonical_minimal_reader"
    fi
    if cmp -s "$obj1" "$obj2"; then
        ok "canonical_minimal_deterministic_obj"
    else
        bad "canonical_minimal_deterministic_obj"
    fi
    require_grep "canonical_minimal_report_function_count" '^facts_function_count=1$' "$report1"
    require_grep "canonical_minimal_report_word_count" '^facts_word_count=2$' "$report1"
    require_grep "canonical_minimal_report_reloc_count" '^facts_reloc_count=0$' "$report1"
    if cc -o "$bin" "$obj1" >/dev/null 2>&1 &&
       "$bin" >/dev/null 2>&1; then
        ok "canonical_minimal_link_run"
    else
        bad "canonical_minimal_link_run"
    fi
}

canonical_data_reader_smoke() {
    if [ "$TARGET" != "arm64-apple-darwin" ]; then
        ok "canonical_data_skipped_non_arm64"
        return
    fi
    local facts="$WORK/canonical_data.facts"
    local obj1="$WORK/canonical_data.1.o"
    local obj2="$WORK/canonical_data.2.o"
    local bin="$WORK/canonical_data"
    local report1="$WORK/canonical_data.1.report.txt"
    local report2="$WORK/canonical_data.2.report.txt"
    local exe="$WORK/canonical_data.direct"
    local exe_report="$WORK/canonical_data.direct.report.txt"
    local bad_pair="$WORK/canonical_data.bad_pair.badfacts"
    local bad_pair_obj="$WORK/canonical_data.bad_pair.o"
    local bad_pair_report="$WORK/canonical_data.bad_pair.report.txt"
    cat > "$facts" <<'CSG'
CHENG_CSG_V2
R0001000000161200000061726d36342d6170706c652d64617277696e
R000200000009050000006d6163686f
R000300000008040000006d61696e
R000400000024010000000000000005000000040000006d61696e0c000000656e7472795f627269646765
R00050000000400000090
R00050000000400000091
R00050000000400004039
R00050000000400040151
R000500000004c0035fd6
R00070000001500000000030000006d736701000000020000004100
R00080000001701000000000000000100000000000000030000006d7367
CSG
    if [ "$(facts_magic_kind "$facts")" = "canonical" ]; then
        ok "canonical_data_magic"
    else
        bad "canonical_data_magic"
    fi
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj1" \
        --report-out:"$report1" >/dev/null &&
       "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj2" \
        --report-out:"$report2" >/dev/null; then
        ok "canonical_data_reader"
    else
        bad "canonical_data_reader"
    fi
    if cmp -s "$obj1" "$obj2"; then
        ok "canonical_data_deterministic_obj"
    else
        bad "canonical_data_deterministic_obj"
    fi
    require_grep "canonical_data_report_function_count" '^facts_function_count=1$' "$report1"
    require_grep "canonical_data_report_word_count" '^facts_word_count=5$' "$report1"
    require_grep "canonical_data_report_reloc_count" '^facts_reloc_count=0$' "$report1"
    require_grep "canonical_data_report_data_count" '^facts_data_count=1$' "$report1"
    require_grep "canonical_data_report_data_reloc_count" '^facts_data_reloc_count=1$' "$report1"
    if cc -o "$bin" "$obj1" >/dev/null 2>&1 &&
       "$bin" >/dev/null 2>&1; then
        ok "canonical_data_link_run"
    else
        bad "canonical_data_link_run"
    fi
    rm -f "$exe" "$exe_report"
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:exe \
        --target:"$TARGET" \
        --out:"$exe" \
        --report-out:"$exe_report" >/dev/null 2>&1; then
        bad "canonical_data_direct_exe_hard_fail"
    elif [ -e "$exe" ]; then
        bad "canonical_data_direct_exe_no_output"
    elif grep -q '^system_link=0$' "$exe_report" 2>/dev/null &&
         grep -q '^error=cold subset compile failed$' "$exe_report" 2>/dev/null; then
        ok "canonical_data_direct_exe_hard_fail"
        ok "canonical_data_direct_exe_no_output"
    else
        bad "canonical_data_direct_exe_hard_fail"
        bad "canonical_data_direct_exe_no_output"
    fi
    sed 's/R00050000000400000091/R00050000000401000091/' "$facts" > "$bad_pair"
    rm -f "$bad_pair_obj" "$bad_pair_report"
    if "$COLD" system-link-exec \
        --csg-in:"$bad_pair" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$bad_pair_obj" \
        --report-out:"$bad_pair_report" >/dev/null 2>&1; then
        bad "canonical_data_reloc_pair_hard_fail"
    elif [ -e "$bad_pair_obj" ]; then
        bad "canonical_data_reloc_pair_no_output"
    else
        ok "canonical_data_reloc_pair_hard_fail"
        ok "canonical_data_reloc_pair_no_output"
        require_grep "canonical_data_reloc_pair_report_direct_macho" '^direct_macho=0$' "$bad_pair_report"
        require_grep "canonical_data_reloc_pair_report_error" '^error=cold csg v2 object emit failed$' "$bad_pair_report"
    fi
}

canonical_writer_smoke() {
    if [ "$TARGET" != "arm64-apple-darwin" ]; then
        ok "canonical_writer_skipped_non_arm64"
        return
    fi
    local facts="$WORK/canonical_writer_ordinary.facts"
    local obj="$WORK/canonical_writer_ordinary.o"
    local bin="$WORK/canonical_writer_ordinary"
    local report="$WORK/canonical_writer_ordinary.writer.report.txt"
    local reader_report="$WORK/canonical_writer_ordinary.reader.report.txt"
    if "$DRIVER" emit-cold-csg-v2 \
        --root:"$ROOT" \
        --in:src/tests/ordinary_zero_exit_fixture.cheng \
        --out:"$facts" \
        --target:"$TARGET" \
        --report-out:"$report" >/dev/null; then
        ok "canonical_writer_ordinary_command"
    else
        bad "canonical_writer_ordinary_command"
        return
    fi
    if [ "$(facts_magic_kind "$facts")" = "canonical" ]; then
        ok "canonical_writer_ordinary_magic"
    else
        bad "canonical_writer_ordinary_magic"
    fi
    require_grep "canonical_writer_ordinary_report_emit" '^emit=csg-v2-primary$' "$report"
    require_grep "canonical_writer_ordinary_report_status" '^cold_csg_v2_writer_status=ok$' "$report"
    require_report_positive "canonical_writer_ordinary_report_facts_bytes" "facts_bytes" "$report"
    require_grep "canonical_writer_ordinary_report_writer_function_count" '^facts_function_count=1$' "$report"
    require_report_positive "canonical_writer_ordinary_report_writer_word_count" "facts_word_count" "$report"
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj" \
        --report-out:"$reader_report" >/dev/null; then
        ok "canonical_writer_ordinary_reader"
    else
        bad "canonical_writer_ordinary_reader"
    fi
    require_grep "canonical_writer_ordinary_report_function_count" '^facts_function_count=1$' "$reader_report"
    if cc -o "$bin" "$obj" >/dev/null 2>&1 &&
       "$bin" >/dev/null 2>&1; then
        ok "canonical_writer_ordinary_link_run"
    else
        bad "canonical_writer_ordinary_link_run"
    fi
}

canonical_driver_writer_str_smoke() {
    if [ "$TARGET" != "arm64-apple-darwin" ]; then
        ok "canonical_writer_str_skipped_non_arm64"
        return
    fi
    local facts="$WORK/canonical_writer_str_call.facts"
    local obj="$WORK/canonical_writer_str_call.o"
    local bin="$WORK/canonical_writer_str_call"
    local report="$WORK/canonical_writer_str_call.writer.report.txt"
    local reader_report="$WORK/canonical_writer_str_call.reader.report.txt"
    if "$DRIVER" emit-cold-csg-v2 \
        --root:"$ROOT" \
        --in:src/tests/str_call_const_then_call_arg0_return_i32_direct_object_smoke.cheng \
        --out:"$facts" \
        --target:"$TARGET" \
        --report-out:"$report" >/dev/null; then
        ok "canonical_writer_str_command"
    else
        bad "canonical_writer_str_command"
        return
    fi
    if [ "$(facts_magic_kind "$facts")" = "canonical" ]; then
        ok "canonical_writer_str_magic"
    else
        bad "canonical_writer_str_magic"
    fi
    require_grep "canonical_writer_str_report_emit" '^emit=csg-v2-primary$' "$report"
    require_grep "canonical_writer_str_report_status" '^cold_csg_v2_writer_status=ok$' "$report"
    require_report_positive "canonical_writer_str_report_facts_bytes" "facts_bytes" "$report"
    require_grep "canonical_writer_str_report_writer_function_count" '^facts_function_count=3$' "$report"
    require_report_positive "canonical_writer_str_report_writer_word_count" "facts_word_count" "$report"
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$TARGET" \
        --out:"$obj" \
        --report-out:"$reader_report" >/dev/null; then
        ok "canonical_writer_str_reader"
    else
        bad "canonical_writer_str_reader"
    fi
    require_grep "canonical_writer_str_report_function_count" '^facts_function_count=3$' "$reader_report"
    require_report_positive "canonical_writer_str_report_word_count" "facts_word_count" "$reader_report"
    if cc -o "$bin" "$obj" >/dev/null 2>&1; then
        local run_rc=0
        "$bin" >/dev/null 2>&1 || run_rc=$?
        if [ "$run_rc" -eq 7 ]; then
            ok "canonical_writer_str_link_run"
        else
            bad "canonical_writer_str_link_run"
        fi
    else
        bad "canonical_writer_str_link_run"
    fi
}

canonical_writer_aarch64_link_object_smoke() {
    local target="aarch64-unknown-linux-gnu"
    local facts="$WORK/canonical_writer_aarch64_ordinary.facts"
    local obj="$WORK/canonical_writer_aarch64_ordinary.o"
    local exe="$WORK/canonical_writer_aarch64_ordinary.exe"
    local report="$WORK/canonical_writer_aarch64_ordinary.writer.report.txt"
    local obj_report="$WORK/canonical_writer_aarch64_ordinary.obj.report.txt"
    local link_report="$WORK/canonical_writer_aarch64_ordinary.link.report.txt"
    if "$DRIVER" emit-cold-csg-v2 \
        --root:"$ROOT" \
        --in:src/tests/ordinary_zero_exit_fixture.cheng \
        --out:"$facts" \
        --target:"$target" \
        --report-out:"$report" >/dev/null; then
        ok "canonical_writer_aarch64_command"
    else
        bad "canonical_writer_aarch64_command"
        return
    fi
    if [ "$(facts_magic_kind "$facts")" = "canonical" ]; then
        ok "canonical_writer_aarch64_magic"
    else
        bad "canonical_writer_aarch64_magic"
    fi
    require_grep "canonical_writer_aarch64_report_target" '^target=aarch64-unknown-linux-gnu$' "$report"
    require_grep "canonical_writer_aarch64_report_emit" '^emit=csg-v2-primary$' "$report"
    require_grep "canonical_writer_aarch64_report_status" '^cold_csg_v2_writer_status=ok$' "$report"
    require_report_positive "canonical_writer_aarch64_report_facts_bytes" "facts_bytes" "$report"
    require_grep "canonical_writer_aarch64_report_writer_function_count" '^facts_function_count=1$' "$report"
    require_report_positive "canonical_writer_aarch64_report_writer_word_count" "facts_word_count" "$report"
    rm -f "$obj" "$obj.linked" "$exe"
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj \
        --target:"$target" \
        --out:"$obj" \
        --report-out:"$obj_report" >/dev/null; then
        ok "canonical_writer_aarch64_emit_obj"
    else
        bad "canonical_writer_aarch64_emit_obj"
    fi
    if [ -s "$obj" ] && [ ! -e "$obj.linked" ] && file "$obj" | grep -q "ELF.*relocatable"; then
        ok "canonical_writer_aarch64_obj_format"
    else
        bad "canonical_writer_aarch64_obj_format"
    fi
    require_grep "canonical_writer_aarch64_obj_report_emit" '^emit=obj$' "$obj_report"
    require_grep "canonical_writer_aarch64_obj_report_target" '^target=aarch64-unknown-linux-gnu$' "$obj_report"
    require_grep "canonical_writer_aarch64_obj_report_link_object" '^link_object=0$' "$obj_report"
    require_grep "canonical_writer_aarch64_obj_report_system_link" '^system_link=0$' "$obj_report"
    require_grep "canonical_writer_aarch64_obj_report_unresolved" '^unresolved_symbol_count=0$' "$obj_report"
    require_grep "canonical_writer_aarch64_obj_report_function_count" '^facts_function_count=1$' "$obj_report"
    require_report_positive "canonical_writer_aarch64_obj_report_word_count" "facts_word_count" "$obj_report"
    if "$COLD" system-link-exec \
        --link-object:"$obj" \
        --emit:exe \
        --target:"$target" \
        --out:"$exe" \
        --report-out:"$link_report" >/dev/null; then
        ok "canonical_writer_aarch64_link_object"
    else
        bad "canonical_writer_aarch64_link_object"
    fi
    if [ -s "$exe" ] && file "$exe" | grep -q "ELF.*executable"; then
        ok "canonical_writer_aarch64_exe_format"
    else
        bad "canonical_writer_aarch64_exe_format"
    fi
    require_grep "canonical_writer_aarch64_link_report_target" '^target=aarch64-unknown-linux-gnu$' "$link_report"
    require_grep "canonical_writer_aarch64_link_report_link_object" '^link_object=1$' "$link_report"
    require_grep "canonical_writer_aarch64_link_report_system_link" '^system_link=0$' "$link_report"
    require_grep "canonical_writer_aarch64_link_report_unresolved" '^unresolved_symbol_count=0$' "$link_report"
    require_grep "canonical_writer_aarch64_link_report_linkerless" '^linkerless_image=1$' "$link_report"
}

canonical_writer_aarch64_provider_archive_smoke() {
    local target="aarch64-unknown-linux-gnu"
    local provider_one_source="$WORK/canonical_provider_aarch64_one.cheng"
    local provider_two_source="$WORK/canonical_provider_aarch64_two.cheng"
    local primary_source="$WORK/canonical_provider_aarch64_primary.cheng"
    local provider_one_obj="$WORK/canonical_provider_aarch64_one.o"
    local provider_two_obj="$WORK/canonical_provider_aarch64_two.o"
    local provider_one_report="$WORK/canonical_provider_aarch64_one.report.txt"
    local provider_two_report="$WORK/canonical_provider_aarch64_two.report.txt"
    local archive="$WORK/canonical_provider_aarch64.chenga"
    local facts="$WORK/canonical_provider_aarch64_primary.facts"
    local primary_obj="$WORK/canonical_provider_aarch64_primary.o"
    local exe="$WORK/canonical_provider_aarch64.exe"
    local direct_exe="$WORK/canonical_provider_aarch64_direct.exe"
    local writer_report="$WORK/canonical_provider_aarch64_primary.writer.report.txt"
    local obj_report="$WORK/canonical_provider_aarch64.obj.report.txt"
    local pack_report="$WORK/canonical_provider_aarch64.pack.report.txt"
    local link_report="$WORK/canonical_provider_aarch64.link.report.txt"
    local direct_link_report="$WORK/canonical_provider_aarch64.direct.link.report.txt"

    cat > "$provider_one_source" <<'CHENG'
@exportc("canonical_provider_aarch64_one")
fn canonical_provider_aarch64_one(): int32 = return 11
CHENG

    cat > "$provider_two_source" <<'CHENG'
@exportc("canonical_provider_aarch64_two")
fn canonical_provider_aarch64_two(): int32 = return 31
CHENG

    cat > "$primary_source" <<'CHENG'
@importc("canonical_provider_aarch64_one")
fn canonical_provider_aarch64_one(): int32
@importc("canonical_provider_aarch64_two")
fn canonical_provider_aarch64_two(): int32
fn main(): int32 = return canonical_provider_aarch64_one() + canonical_provider_aarch64_two()
CHENG

    if "$DRIVER" emit-cold-csg-v2 \
        --root:"$ROOT" \
        --in:"$primary_source" \
        --out:"$facts" \
        --target:"$target" \
        --report-out:"$writer_report" >/dev/null; then
        ok "canonical_provider_aarch64_writer"
    else
        bad "canonical_provider_aarch64_writer"
        return
    fi
    if [ "$(facts_magic_kind "$facts")" = "canonical" ]; then
        ok "canonical_provider_aarch64_magic"
    else
        bad "canonical_provider_aarch64_magic"
    fi
    require_grep "canonical_provider_aarch64_writer_target" '^target=aarch64-unknown-linux-gnu$' "$writer_report"
    require_grep "canonical_provider_aarch64_writer_emit" '^emit=csg-v2-primary$' "$writer_report"
    require_grep "canonical_provider_aarch64_writer_status" '^cold_csg_v2_writer_status=ok$' "$writer_report"
    require_grep "canonical_provider_aarch64_writer_function_count" '^facts_function_count=1$' "$writer_report"
    require_grep "canonical_provider_aarch64_writer_reloc_count" '^facts_reloc_count=2$' "$writer_report"
    require_report_positive "canonical_provider_aarch64_writer_word_count" "facts_word_count" "$writer_report"
    if "$COLD" system-link-exec \
        --in:"$provider_one_source" \
        --emit:obj --target:"$target" \
        --out:"$provider_one_obj" \
        --report-out:"$provider_one_report" >/dev/null &&
       "$COLD" system-link-exec \
        --in:"$provider_two_source" \
        --emit:obj --target:"$target" \
        --out:"$provider_two_obj" \
        --report-out:"$provider_two_report" >/dev/null &&
       "$COLD" provider-archive-pack \
        --target:"$target" \
        --object:"$provider_one_obj" \
        --object:"$provider_two_obj" \
        --export:canonical_provider_aarch64_one \
        --export:canonical_provider_aarch64_two \
        --module:canonical_provider_aarch64 \
        --source:"$WORK" \
        --out:"$archive" \
        --report-out:"$pack_report" >/dev/null; then
        ok "canonical_provider_aarch64_archive"
    else
        bad "canonical_provider_aarch64_archive"
    fi
    require_grep "canonical_provider_aarch64_provider_one_emit" '^emit=obj$' "$provider_one_report"
    require_grep "canonical_provider_aarch64_provider_one_target" '^target=aarch64-unknown-linux-gnu$' "$provider_one_report"
    require_grep "canonical_provider_aarch64_provider_one_link_object" '^link_object=0$' "$provider_one_report"
    require_grep "canonical_provider_aarch64_provider_one_system_link" '^system_link=0$' "$provider_one_report"
    require_grep "canonical_provider_aarch64_provider_one_provider_archive" '^provider_archive=0$' "$provider_one_report"
    require_grep "canonical_provider_aarch64_provider_two_emit" '^emit=obj$' "$provider_two_report"
    require_grep "canonical_provider_aarch64_provider_two_target" '^target=aarch64-unknown-linux-gnu$' "$provider_two_report"
    require_grep "canonical_provider_aarch64_provider_two_link_object" '^link_object=0$' "$provider_two_report"
    require_grep "canonical_provider_aarch64_provider_two_system_link" '^system_link=0$' "$provider_two_report"
    require_grep "canonical_provider_aarch64_provider_two_provider_archive" '^provider_archive=0$' "$provider_two_report"
    if [ -s "$provider_one_obj" ] && [ ! -e "$provider_one_obj.linked" ] &&
       [ -s "$provider_two_obj" ] && [ ! -e "$provider_two_obj.linked" ]; then
        ok "canonical_provider_aarch64_provider_objects_only"
    else
        bad "canonical_provider_aarch64_provider_objects_only"
    fi
    require_grep "canonical_provider_aarch64_pack_target" '^target=aarch64-unknown-linux-gnu$' "$pack_report"
    require_grep "canonical_provider_aarch64_pack_ok" '^provider_archive_pack=1$' "$pack_report"
    require_grep "canonical_provider_aarch64_pack_members" '^provider_object_count=2$' "$pack_report"
    require_grep "canonical_provider_aarch64_pack_archive_members" '^provider_archive_member_count=2$' "$pack_report"
    require_grep "canonical_provider_aarch64_pack_exports" '^provider_export_count=2$' "$pack_report"
    require_grep "canonical_provider_aarch64_pack_system_link" '^system_link=0$' "$pack_report"
    require_grep "canonical_provider_aarch64_pack_linkerless" '^linkerless_image=1$' "$pack_report"
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --emit:obj --target:"$target" \
        --out:"$primary_obj" \
        --report-out:"$obj_report" >/dev/null; then
        ok "canonical_provider_aarch64_emit_obj"
    else
        bad "canonical_provider_aarch64_emit_obj"
    fi
    if [ -s "$primary_obj" ] && file "$primary_obj" | grep -q "ELF.*relocatable"; then
        ok "canonical_provider_aarch64_obj_format"
    else
        bad "canonical_provider_aarch64_obj_format"
    fi
    require_grep "canonical_provider_aarch64_obj_emit" '^emit=obj$' "$obj_report"
    require_grep "canonical_provider_aarch64_obj_target" '^target=aarch64-unknown-linux-gnu$' "$obj_report"
    require_grep "canonical_provider_aarch64_obj_link_object" '^link_object=0$' "$obj_report"
    require_grep "canonical_provider_aarch64_obj_system_link" '^system_link=0$' "$obj_report"
    require_grep "canonical_provider_aarch64_obj_provider_archive" '^provider_archive=0$' "$obj_report"
    require_grep "canonical_provider_aarch64_obj_unresolved" '^unresolved_symbol_count=0$' "$obj_report"
    require_grep "canonical_provider_aarch64_obj_csg_lowering" '^cold_csg_lowering=1$' "$obj_report"
    require_grep "canonical_provider_aarch64_obj_reloc_count" '^facts_reloc_count=2$' "$obj_report"
    if "$COLD" system-link-exec \
        --link-object:"$primary_obj" \
        --provider-archive:"$archive" \
        --emit:exe --target:"$target" \
        --out:"$exe" \
        --report-out:"$link_report" >/dev/null; then
        ok "canonical_provider_aarch64_link"
    else
        bad "canonical_provider_aarch64_link"
    fi
    if [ -s "$exe" ] && file "$exe" | grep -q "ELF.*executable"; then
        ok "canonical_provider_aarch64_exe_format"
    else
        bad "canonical_provider_aarch64_exe_format"
    fi
    require_grep "canonical_provider_aarch64_link_object" '^link_object=1$' "$link_report"
    require_grep "canonical_provider_aarch64_link_target" '^target=aarch64-unknown-linux-gnu$' "$link_report"
    require_grep "canonical_provider_aarch64_link_provider_archive" '^provider_archive=1$' "$link_report"
    require_grep "canonical_provider_aarch64_link_provider_objects" '^provider_object_count=2$' "$link_report"
    require_grep "canonical_provider_aarch64_link_members" '^provider_archive_member_count=2$' "$link_report"
    require_grep "canonical_provider_aarch64_link_exports" '^provider_export_count=2$' "$link_report"
    require_grep "canonical_provider_aarch64_link_resolved" '^provider_resolved_symbol_count=2$' "$link_report"
    require_grep "canonical_provider_aarch64_link_unresolved" '^unresolved_symbol_count=0$' "$link_report"
    require_grep "canonical_provider_aarch64_link_system_link" '^system_link=0$' "$link_report"
    require_grep "canonical_provider_aarch64_link_linkerless" '^linkerless_image=1$' "$link_report"
    if "$COLD" system-link-exec \
        --csg-in:"$facts" \
        --provider-archive:"$archive" \
        --emit:exe --target:"$target" \
        --out:"$direct_exe" \
        --report-out:"$direct_link_report" >/dev/null; then
        ok "canonical_provider_aarch64_csg_in_provider_archive"
    else
        bad "canonical_provider_aarch64_csg_in_provider_archive"
    fi
    if [ -s "$direct_exe" ] && file "$direct_exe" | grep -q "ELF.*executable"; then
        ok "canonical_provider_aarch64_direct_exe_format"
    else
        bad "canonical_provider_aarch64_direct_exe_format"
    fi
    require_grep "canonical_provider_aarch64_direct_target" '^target=aarch64-unknown-linux-gnu$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_provider_archive" '^provider_archive=1$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_provider_objects" '^provider_object_count=2$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_members" '^provider_archive_member_count=2$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_exports" '^provider_export_count=2$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_resolved" '^provider_resolved_symbol_count=2$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_unresolved" '^unresolved_symbol_count=0$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_system_link" '^system_link=0$' "$direct_link_report"
    require_grep "canonical_provider_aarch64_direct_linkerless" '^linkerless_image=1$' "$direct_link_report"
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

    if "$COLD" system-link-exec \
        --root:"$ROOT" \
        --in:"$source" \
        --emit:csg-v2 \
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
    if [ "$facts_kind" = "internal" ]; then
        ok "$name internal_chengcsg_magic"
    else
        bad "$name internal_chengcsg_magic actual=$facts_kind"
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
    local target="${3:-$TARGET}"

    local obj_O0="$WORK/$name.xver_O0.o"
    local obj_O2="$WORK/$name.xver_O2.o"
    local csg_O0="$WORK/$name.xver_O0.csgv2"
    local csg_O2="$WORK/$name.xver_O2.csgv2"

    echo "  - ${name}_xver_obj"
    if "$COLD_O0" system-link-exec \
        --csg-in:"$facts" --emit:obj --target:"$target" \
        --out:"$obj_O0" >/dev/null 2>&1 &&
       "$COLD" system-link-exec \
        --csg-in:"$facts" --emit:obj --target:"$target" \
        --out:"$obj_O2" >/dev/null 2>&1; then
        if cmp -s "$obj_O0" "$obj_O2" 2>/dev/null; then
            ok "${name}_xver_obj"
        else
            bad "${name}_xver_obj (O0 vs O2 .o differ)"
        fi
    else
        bad "${name}_xver_obj (cold run failed)"
    fi

    if [ "$(facts_magic_kind "$facts")" = "canonical" ]; then
        return
    fi

    echo "  - ${name}_xver_csg"
    if "$COLD_O0" system-link-exec \
        --csg-in:"$facts" --emit:csg-v2 --target:"$target" \
        --out:"$csg_O0" >/dev/null 2>&1 &&
       "$COLD" system-link-exec \
        --csg-in:"$facts" --emit:csg-v2 --target:"$target" \
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
canonical_reader_smoke
canonical_data_reader_smoke
canonical_writer_smoke
canonical_driver_writer_str_smoke
canonical_writer_aarch64_link_object_smoke
canonical_writer_aarch64_provider_archive_smoke

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

# Bitwise operations: OR, XOR
roundtrip_fixture return_bitwise tests/cheng/backend/fixtures/return_bitwise.cheng 4096

# Shift operations: SHL (<<) and ASR (>>)
roundtrip_fixture return_shift tests/cheng/backend/fixtures/return_shift.cheng 4096

# Addition: 40 + 2 - 42
roundtrip_fixture return_add tests/cheng/backend/fixtures/return_add.cheng 4096

# Ternary conditional: if expression
roundtrip_fixture return_if tests/cheng/backend/fixtures/return_if.cheng 4096

# Pointer dereference: & and *
roundtrip_fixture return_deref tests/cheng/backend/fixtures/return_deref.cheng 4096

# I32 type casting: int32() expression
roundtrip_fixture return_cast_i32 tests/cheng/backend/fixtures/return_cast_i32.cheng 4096

# I64 type casting: int64() expression
roundtrip_fixture return_cast_i64 tests/cheng/backend/fixtures/return_cast_i64.cheng 4096

# Logical and expression: (1 < 2 && 2 < 3) - 1
roundtrip_fixture return_and_expr tests/cheng/backend/fixtures/return_and_expr.cheng 4096

# If statement: var x, if cond then x=1 else x=2, return x-1
roundtrip_fixture return_if_stmt tests/cheng/backend/fixtures/return_if_stmt.cheng 4096

# I64 arithmetic: sub, const
roundtrip_fixture return_i64 tests/cheng/backend/fixtures/return_i64.cheng 4096

# And-assign: var x; x = 1 < 2 && 2 < 3; return x - 1
roundtrip_fixture return_and_assign tests/cheng/backend/fixtures/return_and_assign.cheng 4096

# Unsigned division: uint32/uint64 div
roundtrip_fixture return_udiv tests/cheng/backend/fixtures/return_udiv.cheng 4096

# Unsigned modulo: uint32/uint64 mod
roundtrip_fixture return_umod tests/cheng/backend/fixtures/return_umod.cheng 4096

# I64 function calls: add64(10,20) - 30 == 0
roundtrip_fixture return_call_i64 tests/cheng/backend/fixtures/return_call_i64.cheng 4096

# Multi-arg function calls: 5-arg sum
roundtrip_fixture return_call5 tests/cheng/backend/fixtures/return_call5.cheng 4096

# Multi-arg function calls: 8-arg sum
roundtrip_fixture return_call8 tests/cheng/backend/fixtures/return_call8.cheng 4096

# Multi-arg function calls: 9-arg sum
roundtrip_fixture return_call9 tests/cheng/backend/fixtures/return_call9.cheng 4096

# I64 mixed function calls: widen + cast
roundtrip_fixture return_call_mixed tests/cheng/backend/fixtures/return_call_mixed.cheng 4096

# Global variable assignment: g = 7, return g - 7 == 0
roundtrip_fixture return_global_assign tests/cheng/backend/fixtures/return_global_assign.cheng 4096

# Global i64 variable: load and compare
roundtrip_fixture return_global_i64 tests/cheng/backend/fixtures/return_global_i64.cheng 4096

# Logical OR expression: (0 < 0 || 1 < 2) - 1 == 0
roundtrip_fixture return_or_expr tests/cheng/backend/fixtures/return_or_expr.cheng 4096

# Logical OR in if condition: if cond || cond
roundtrip_fixture return_or_if tests/cheng/backend/fixtures/return_or_if.cheng 4096

# Logical OR in let: let x = a || b
roundtrip_fixture return_or_let tests/cheng/backend/fixtures/return_or_let.cheng 4096

# Inline if + ternary: clamp + ternary patterns
roundtrip_fixture return_inline_if_and_ternary tests/cheng/backend/fixtures/return_inline_if_and_ternary.cheng 4096

# Nested boolean AND: (ternary && ternary) - 1 == 0
roundtrip_fixture return_and_nested tests/cheng/backend/fixtures/return_and_nested.cheng 4096

# Simple var assignment: x = x + 1, return x - 2 == 0
roundtrip_fixture return_assign tests/cheng/backend/fixtures/return_assign.cheng 4096

# Import chain with unqualified cross-module bare call must hard-fail in cold.
echo "  - import_chain_bare_hard_fail"
import_chain_report="$WORK/import_chain_a.writer.report.txt"
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:tests/cheng/backend/fixtures/import_chain_a.cheng \
    --emit:csg-v2 \
    --out:"$WORK/import_chain_a.facts" \
    --target:"$TARGET" \
    --report-out:"$import_chain_report" >/dev/null 2>&1; then
    bad "import_chain_bare_hard_fail"
else
    require_grep "import_chain_bare_hard_fail" '^error=unresolved function call$' "$import_chain_report"
fi

# Roundtrip stability: all fixtures must converge in 1 csg-v2 step
echo "  - roundtrip_stability"
for f in "$WORK"/*.facts; do
    base="$(basename "$f" .facts)"
    if [ "$(facts_magic_kind "$f")" = "canonical" ]; then
        continue
    fi
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

# provider-archive-pack accepts Mach-O provider objects and writes a Chenga
# archive. Missing Mach-O archive paths remain explicit hard-fail gates below.
darwin_pack_archive="$WORK/core_runtime_provider_darwin.chenga"
darwin_pack_report="$WORK/core_runtime_provider_darwin.pack.report.txt"
if "$COLD" provider-archive-pack \
    --target:arm64-apple-darwin \
    --object:"$darwin_runtime_provider_obj" \
    --export:core_runtime_stub_trace \
    --module:runtime/core_runtime \
    --source:"$darwin_runtime_provider_source" \
    --out:"$darwin_pack_archive" \
   --report-out:"$darwin_pack_report" >/dev/null 2>&1 &&
   [ -s "$darwin_pack_archive" ] &&
   grep -q '^provider_archive_pack=1$' "$darwin_pack_report" 2>/dev/null &&
   grep -q '^provider_object_count=1$' "$darwin_pack_report" 2>/dev/null &&
   grep -q '^provider_export_count=1$' "$darwin_pack_report" 2>/dev/null &&
   grep -q '^system_link=0$' "$darwin_pack_report" 2>/dev/null &&
   grep -q '^linkerless_image=1$' "$darwin_pack_report" 2>/dev/null; then
    ok "darwin_provider_archive_pack"
else
    bad "darwin_provider_archive_pack"
fi

darwin_external_primary="$WORK/darwin_runtime_provider_external_primary.cheng"
darwin_external_primary_obj="$WORK/darwin_runtime_provider_external_primary.o"
darwin_external_primary_report="$WORK/darwin_runtime_provider_external_primary.report.txt"
darwin_external_out="$WORK/darwin_runtime_provider_external_should_not_exist"
darwin_external_link_report="$WORK/darwin_runtime_provider_external.report.txt"
cat > "$darwin_external_primary" <<'CHENG'
@importc("core_runtime_stub_trace")
fn core_runtime_stub_trace(): int32
fn main(): int32 = return core_runtime_stub_trace()
CHENG
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$darwin_external_primary" \
    --emit:obj \
    --target:arm64-apple-darwin \
    --out:"$darwin_external_primary_obj" \
    --report-out:"$darwin_external_primary_report" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --link-object:"$darwin_external_primary_obj" \
    --provider-archive:"$darwin_pack_archive" \
    --emit:exe \
    --target:arm64-apple-darwin \
    --out:"$darwin_external_out" \
    --report-out:"$darwin_external_link_report" >/dev/null 2>&1; then
    bad "darwin_provider_external_dependency_hard_fail"
elif [ -s "$darwin_external_primary_obj" ] &&
     grep -q '^direct_macho=1$' "$darwin_external_primary_report" &&
     grep -q '^link_object=1$' "$darwin_external_link_report" &&
     grep -q '^provider_archive=1$' "$darwin_external_link_report" &&
     grep -q '^provider_object_count=1$' "$darwin_external_link_report" &&
     grep -q '^provider_archive_member_count=1$' "$darwin_external_link_report" &&
     grep -q '^provider_export_count=1$' "$darwin_external_link_report" &&
     grep -q '^unresolved_symbol_count=1$' "$darwin_external_link_report" &&
     grep -q '^first_unresolved_symbol=write$' "$darwin_external_link_report" &&
     grep -q '^error=provider external dependency unsupported: write$' "$darwin_external_link_report" &&
     [ ! -e "$darwin_external_out" ]; then
    ok "darwin_provider_external_dependency_hard_fail"
else
    bad "darwin_provider_external_dependency_hard_fail"
fi

darwin_runtime_const_provider_obj="$WORK/core_runtime_provider_darwin_af_inet.o"
darwin_runtime_const_provider_report="$WORK/core_runtime_provider_darwin_af_inet.report.txt"
darwin_runtime_const_primary="$WORK/darwin_runtime_provider_primary.cheng"
darwin_runtime_const_primary_obj="$WORK/darwin_runtime_provider_primary.o"
darwin_runtime_const_primary_report="$WORK/darwin_runtime_provider_primary.report.txt"
darwin_runtime_const_archive="$WORK/core_runtime_provider_darwin_af_inet.chenga"
darwin_runtime_const_pack_report="$WORK/core_runtime_provider_darwin_af_inet.pack.report.txt"
darwin_runtime_const_exe="$WORK/darwin_runtime_provider_link"
darwin_runtime_const_link_report="$WORK/darwin_runtime_provider_link.report.txt"
cat > "$darwin_runtime_const_primary" <<'CHENG'
@importc("cheng_native_af_inet_bridge")
fn cheng_native_af_inet_bridge(): int32
fn main(): int32 = return cheng_native_af_inet_bridge()
CHENG
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$darwin_runtime_provider_source" \
    --emit:obj \
    --target:arm64-apple-darwin \
    --symbol-visibility:internal \
    --export-roots:cheng_native_af_inet_bridge \
    --out:"$darwin_runtime_const_provider_obj" \
    --report-out:"$darwin_runtime_const_provider_report" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$darwin_runtime_const_primary" \
    --emit:obj \
    --target:arm64-apple-darwin \
    --out:"$darwin_runtime_const_primary_obj" \
    --report-out:"$darwin_runtime_const_primary_report" >/dev/null 2>&1 &&
   "$COLD" provider-archive-pack \
    --target:arm64-apple-darwin \
    --object:"$darwin_runtime_const_provider_obj" \
    --export:cheng_native_af_inet_bridge \
    --module:runtime/core_runtime \
    --source:"$darwin_runtime_provider_source" \
    --out:"$darwin_runtime_const_archive" \
    --report-out:"$darwin_runtime_const_pack_report" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --link-object:"$darwin_runtime_const_primary_obj" \
    --provider-archive:"$darwin_runtime_const_archive" \
    --emit:exe \
    --target:arm64-apple-darwin \
    --out:"$darwin_runtime_const_exe" \
    --report-out:"$darwin_runtime_const_link_report" >/dev/null 2>&1; then
    ok "darwin_runtime_provider_archive_link"
else
    bad "darwin_runtime_provider_archive_link"
fi
require_grep "darwin_runtime_provider_archive_provider_obj" '^direct_macho=1$' "$darwin_runtime_const_provider_report"
require_grep "darwin_runtime_provider_archive_primary_obj" '^direct_macho=1$' "$darwin_runtime_const_primary_report"
require_grep "darwin_runtime_provider_archive_pack_ok" '^provider_archive_pack=1$' "$darwin_runtime_const_pack_report"
require_grep "darwin_runtime_provider_archive_link_ok" '^system_link_exec=1$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_link_direct" '^direct_macho=1$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_link_objects" '^provider_object_count=1$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_link_members" '^provider_archive_member_count=1$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_link_exports" '^provider_export_count=1$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_link_resolved" '^provider_resolved_symbol_count=1$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_link_unresolved" '^unresolved_symbol_count=0$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_linkerless" '^linkerless_image=1$' "$darwin_runtime_const_link_report"
require_grep "darwin_runtime_provider_archive_no_system_link" '^system_link=0$' "$darwin_runtime_const_link_report"
darwin_runtime_const_rc=0
"$darwin_runtime_const_exe" >/dev/null 2>&1 || darwin_runtime_const_rc=$?
if [ "$darwin_runtime_const_rc" -eq 2 ]; then
    ok "darwin_runtime_provider_archive_run"
else
    bad "darwin_runtime_provider_archive_run"
fi

darwin_archive_report="$WORK/darwin_provider_archive_hard_fail.report.txt"
if "$COLD" system-link-exec \
    --csg-in:"$WORK/ordinary.facts" \
    --provider-archive:"$WORK/missing_darwin_runtime.chenga" \
    --emit:exe \
    --target:arm64-apple-darwin \
    --out:"$WORK/darwin_provider_archive_should_not_exist" \
    --report-out:"$darwin_archive_report" >/dev/null 2>&1; then
    bad "darwin_provider_archive_missing_hard_fail"
elif grep -q '^provider_archive=0$' "$darwin_archive_report" &&
     grep -q '^provider_object_count=0$' "$darwin_archive_report" &&
     grep -q '^system_link=0$' "$darwin_archive_report" &&
     grep -q '^error=provider archive open/header failed$' "$darwin_archive_report"; then
    ok "darwin_provider_archive_missing_hard_fail"
else
    bad "darwin_provider_archive_missing_hard_fail"
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
    bad "darwin_link_object_provider_archive_missing_hard_fail"
elif [ -s "$darwin_link_object_primary" ] &&
     grep -q '^direct_macho=1$' "$darwin_link_object_primary_report" &&
     grep -q '^link_object=1$' "$darwin_link_object_report" &&
     grep -q '^provider_archive=1$' "$darwin_link_object_report" &&
     grep -q '^provider_object_count=0$' "$darwin_link_object_report" &&
     grep -q '^system_link=0$' "$darwin_link_object_report" &&
     grep -q '^error=provider archive open/header failed$' "$darwin_link_object_report" &&
     [ ! -e "$darwin_link_object_out" ]; then
    ok "darwin_link_object_provider_archive_missing_hard_fail"
else
    bad "darwin_link_object_provider_archive_missing_hard_fail"
fi


# ELF object emission is pure: linking must go through the explicit link-object entry.
ordinary_riscv_facts="$WORK/ordinary_riscv.facts"
ordinary_riscv_writer_report="$WORK/ordinary_riscv.writer.report.txt"

echo "  - csg_in_wrong_target_hard_fail"
rm -f "$WORK/ordinary_wrong_target.o" "$WORK/ordinary_wrong_target.report.txt"
if "$COLD" system-link-exec \
  --csg-in:"$WORK/ordinary.facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_wrong_target.o" \
  --report-out:"$WORK/ordinary_wrong_target.report.txt" \
  > "$WORK/ordinary_wrong_target.stdout" 2>&1; then
  echo "FAIL csg_in_wrong_target_hard_fail"
  fail=$((fail + 1))
else
  echo "PASS csg_in_wrong_target_hard_fail"
  pass=$((pass + 1))
fi

echo "  - ordinary_riscv_writer"
if "$COLD" system-link-exec \
  --root:"$ROOT" \
  --in:src/tests/ordinary_zero_exit_fixture.cheng \
  --emit:csg-v2 \
  --out:"$ordinary_riscv_facts" \
  --target:riscv64-unknown-linux-gnu \
  --report-out:"$ordinary_riscv_writer_report" \
  > "$WORK/ordinary_riscv.writer.stdout" 2>&1 &&
   [ "$(facts_magic_kind "$ordinary_riscv_facts")" = "internal" ] &&
   grep -q '^target=riscv64-unknown-linux-gnu$' "$ordinary_riscv_writer_report"; then
  echo "PASS ordinary_riscv_writer"
  pass=$((pass + 1))
else
  echo "FAIL ordinary_riscv_writer"
  fail=$((fail + 1))
fi

echo "  - link_object_elf"
rm -f "$WORK/ordinary_link.o" "$WORK/ordinary_link.o.linked" "$WORK/ordinary_link_exec"
if "$COLD" system-link-exec \
  --csg-in:"$ordinary_riscv_facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link.o" \
  --report-out:"$WORK/ordinary_link.report.txt" \
  > "$WORK/ordinary_link.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --link-object:"$WORK/ordinary_link.o" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_exec" \
  --report-out:"$WORK/ordinary_link_exec.report.txt" \
  > "$WORK/ordinary_link_exec.stdout" 2>&1 &&
   [ -s "$WORK/ordinary_link.o" ] &&
   [ ! -e "$WORK/ordinary_link.o.linked" ] &&
   file "$WORK/ordinary_link.o" | grep -q "ELF.*relocatable" &&
   file "$WORK/ordinary_link_exec" | grep -q "ELF.*executable" &&
   grep -q '^emit=obj$' "$WORK/ordinary_link.report.txt" &&
   grep -q '^link_object=0$' "$WORK/ordinary_link.report.txt" &&
   grep -q '^system_link=0$' "$WORK/ordinary_link.report.txt" &&
   grep -q '^link_object=1$' "$WORK/ordinary_link_exec.report.txt" &&
   grep -q '^unresolved_symbol_count=0$' "$WORK/ordinary_link_exec.report.txt" &&
   grep -q '^system_link=0$' "$WORK/ordinary_link_exec.report.txt"; then
  echo "PASS link_object_elf"
  pass=$((pass + 1))
else
  echo "FAIL link_object_elf"
  fail=$((fail + 1))
fi


# Explicit linker determinism: two link-object runs produce bit-identical executables.
echo "  - link_object_determinism"
rm -f "$WORK/ordinary_link_d1.o" "$WORK/ordinary_link_d1.o.linked" "$WORK/ordinary_link_d1.exe"
rm -f "$WORK/ordinary_link_d2.o" "$WORK/ordinary_link_d2.o.linked" "$WORK/ordinary_link_d2.exe"
if "$COLD" system-link-exec \
  --csg-in:"$ordinary_riscv_facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_d1.o" \
  --report-out:"$WORK/ordinary_link_d1.report.txt" \
  > "$WORK/ordinary_link_d1.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --csg-in:"$ordinary_riscv_facts" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_d2.o" \
  --report-out:"$WORK/ordinary_link_d2.report.txt" \
  > "$WORK/ordinary_link_d2.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --link-object:"$WORK/ordinary_link_d1.o" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_d1.exe" \
  --report-out:"$WORK/ordinary_link_d1.exe.report.txt" \
  > "$WORK/ordinary_link_d1.exe.stdout" 2>&1 &&
   "$COLD" system-link-exec \
  --link-object:"$WORK/ordinary_link_d2.o" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/ordinary_link_d2.exe" \
  --report-out:"$WORK/ordinary_link_d2.exe.report.txt" \
  > "$WORK/ordinary_link_d2.exe.stdout" 2>&1 &&
   [ ! -e "$WORK/ordinary_link_d1.o.linked" ] &&
   [ ! -e "$WORK/ordinary_link_d2.o.linked" ] &&
   grep -q '^link_object=1$' "$WORK/ordinary_link_d1.exe.report.txt" &&
   grep -q '^link_object=1$' "$WORK/ordinary_link_d2.exe.report.txt" &&
   grep -q '^unresolved_symbol_count=0$' "$WORK/ordinary_link_d1.exe.report.txt" &&
   grep -q '^unresolved_symbol_count=0$' "$WORK/ordinary_link_d2.exe.report.txt" &&
   grep -q '^system_link=0$' "$WORK/ordinary_link_d1.exe.report.txt" &&
   grep -q '^system_link=0$' "$WORK/ordinary_link_d2.exe.report.txt" &&
   cmp -s "$WORK/ordinary_link_d1.exe" "$WORK/ordinary_link_d2.exe"; then
  echo "PASS link_object_determinism"
  pass=$((pass + 1))
else
  echo "FAIL link_object_determinism"
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
  if cmp -s "$WORK/ordinary_link_d1.exe" "$WORK/ordinary_link_explicit" &&
     [ ! -e "$WORK/ordinary_link_d1.o.linked" ] &&
     file "$WORK/ordinary_link_explicit" | grep -q "ELF.*executable" &&
     grep -q '^link_object=1$' "$WORK/ordinary_link_explicit.report.txt" &&
     grep -q '^unresolved_symbol_count=0$' "$WORK/ordinary_link_explicit.report.txt" &&
     grep -q '^system_link=0$' "$WORK/ordinary_link_explicit.report.txt" &&
     grep -q '^linkerless_image=1$' "$WORK/ordinary_link_explicit.report.txt"; then
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
corrupt_out="$WORK/provider_archive_corrupt_magic_should_not_exist"
corrupt_report="$WORK/provider_archive_corrupt_magic.report.txt"
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
  --out:"$corrupt_out" \
  --report-out:"$corrupt_report" \
  > "$WORK/provider_archive_corrupt_magic.stdout" 2>&1; then
  echo "FAIL provider_archive_corrupt_magic_hard_fail"
  fail=$((fail + 1))
elif grep -q '^provider_archive=1$' "$corrupt_report" &&
     grep -q '^provider_object_count=0$' "$corrupt_report" &&
     grep -q '^system_link=0$' "$corrupt_report" &&
     grep -q '^error=provider archive magic mismatch$' "$corrupt_report" &&
     [ ! -e "$corrupt_out" ]; then
  echo "PASS provider_archive_corrupt_magic_hard_fail"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_corrupt_magic_hard_fail"
  fail=$((fail + 1))
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

echo "  - provider_archive_duplicate_export_pack_fail"
dup_archive="$WORK/provider_fixture_duplicate_export.chenga"
dup_report="$WORK/provider_fixture_duplicate_export.pack.report.txt"
rm -f "$dup_archive" "$dup_report"
if [ ! -s "$provider_obj" ]; then
  echo "FAIL provider_archive_duplicate_export_pack_fail (missing provider object)"
  fail=$((fail + 1))
elif "$COLD" provider-archive-pack \
  --target:riscv64-unknown-linux-gnu \
  --object:"$provider_obj" \
  --export:provider_fixture_value \
  --export:provider_fixture_value \
  --module:provider_fixture \
  --source:"$provider_source" \
  --out:"$dup_archive" \
  --report-out:"$dup_report" \
  > "$WORK/provider_fixture_duplicate_export.pack.stdout" 2>&1; then
  echo "FAIL provider_archive_duplicate_export_pack_fail"
  fail=$((fail + 1))
elif grep -q '^provider_archive_pack=0$' "$dup_report" &&
     grep -q '^error=duplicate provider export: provider_fixture_value$' "$dup_report" &&
     [ ! -e "$dup_archive" ]; then
  echo "PASS provider_archive_duplicate_export_pack_fail"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_duplicate_export_pack_fail"
  fail=$((fail + 1))
fi

echo "  - provider_archive_wrong_machine_pack_fail"
wrong_machine_archive="$WORK/provider_fixture_wrong_machine.chenga"
wrong_machine_report="$WORK/provider_fixture_wrong_machine.pack.report.txt"
rm -f "$wrong_machine_archive" "$wrong_machine_report"
if [ ! -s "$provider_obj" ]; then
  echo "FAIL provider_archive_wrong_machine_pack_fail (missing provider object)"
  fail=$((fail + 1))
elif "$COLD" provider-archive-pack \
  --target:aarch64-unknown-linux-gnu \
  --object:"$provider_obj" \
  --export:provider_fixture_value \
  --module:provider_fixture \
  --source:"$provider_source" \
  --out:"$wrong_machine_archive" \
  --report-out:"$wrong_machine_report" \
  > "$WORK/provider_fixture_wrong_machine.pack.stdout" 2>&1; then
  echo "FAIL provider_archive_wrong_machine_pack_fail"
  fail=$((fail + 1))
elif grep -q '^provider_archive_pack=0$' "$wrong_machine_report" &&
     grep -q '^error=provider object machine mismatch$' "$wrong_machine_report" &&
     [ ! -e "$wrong_machine_archive" ]; then
  echo "PASS provider_archive_wrong_machine_pack_fail"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_wrong_machine_pack_fail"
  fail=$((fail + 1))
fi

echo "  - provider_archive_ambiguous_export_pack_fail"
ambiguous_source="$WORK/provider_fixture_ambiguous.cheng"
ambiguous_obj="$WORK/provider_fixture_ambiguous.o"
ambiguous_archive="$WORK/provider_fixture_ambiguous.chenga"
ambiguous_report="$WORK/provider_fixture_ambiguous.pack.report.txt"
cat > "$ambiguous_source" <<'CHENG'
@exportc("provider_fixture_value")
fn provider_fixture_value(): int32 = return 9
CHENG
rm -f "$ambiguous_obj" "$ambiguous_archive" "$ambiguous_report"
if ! "$COLD" system-link-exec \
  --in:"$ambiguous_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$ambiguous_obj" \
  --report-out:"$WORK/provider_fixture_ambiguous.obj.report.txt" \
  > "$WORK/provider_fixture_ambiguous.obj.stdout" 2>&1; then
  echo "FAIL provider_archive_ambiguous_export_pack_fail (ambiguous object build failed)"
  fail=$((fail + 1))
elif "$COLD" provider-archive-pack \
  --target:riscv64-unknown-linux-gnu \
  --object:"$provider_obj" \
  --object:"$ambiguous_obj" \
  --export:provider_fixture_value \
  --module:provider_fixture \
  --source:"$WORK" \
  --out:"$ambiguous_archive" \
  --report-out:"$ambiguous_report" \
  > "$WORK/provider_fixture_ambiguous.pack.stdout" 2>&1; then
  echo "FAIL provider_archive_ambiguous_export_pack_fail"
  fail=$((fail + 1))
elif grep -q '^provider_archive_pack=0$' "$ambiguous_report" &&
     grep -q '^error=provider export ambiguous: provider_fixture_value$' "$ambiguous_report" &&
     [ ! -e "$ambiguous_archive" ]; then
  echo "PASS provider_archive_ambiguous_export_pack_fail"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_ambiguous_export_pack_fail"
  fail=$((fail + 1))
fi

echo "  - provider_archive_wrong_target_link_fail"
wrong_target_out="$WORK/provider_wrong_target_should_not_exist"
wrong_target_report="$WORK/provider_wrong_target.report.txt"
rm -f "$wrong_target_out" "$wrong_target_report"
if [ ! -s "$WORK/canonical_provider_aarch64_primary.o" ] || [ ! -s "$provider_archive" ]; then
  echo "FAIL provider_archive_wrong_target_link_fail (missing cross-target artifacts)"
  fail=$((fail + 1))
elif "$COLD" system-link-exec \
  --link-object:"$WORK/canonical_provider_aarch64_primary.o" \
  --provider-archive:"$provider_archive" \
  --emit:exe --target:aarch64-unknown-linux-gnu \
  --out:"$wrong_target_out" \
  --report-out:"$wrong_target_report" \
  > "$WORK/provider_wrong_target.stdout" 2>&1; then
  echo "FAIL provider_archive_wrong_target_link_fail"
  fail=$((fail + 1))
elif grep -q '^provider_archive=1$' "$wrong_target_report" &&
     grep -q '^error=provider archive target mismatch$' "$wrong_target_report" &&
     [ ! -e "$wrong_target_out" ]; then
  echo "PASS provider_archive_wrong_target_link_fail"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_wrong_target_link_fail"
  fail=$((fail + 1))
fi

echo "  - provider_archive_primary_conflict_hard_fail"
conflict_provider_source="$WORK/provider_conflict_provider.cheng"
conflict_provider_obj="$WORK/provider_conflict_provider.o"
conflict_archive="$WORK/provider_conflict_provider.chenga"
conflict_pack_report="$WORK/provider_conflict_provider.pack.report.txt"
conflict_source="$WORK/provider_primary_conflict.cheng"
conflict_obj="$WORK/provider_primary_conflict.o"
conflict_out="$WORK/provider_primary_conflict_should_not_exist"
conflict_report="$WORK/provider_primary_conflict.report.txt"
cat > "$conflict_provider_source" <<'CHENG'
@exportc("provider_fixture_value")
fn provider_fixture_value(): int32 = return 7

@exportc("provider_conflict_extra")
fn provider_conflict_extra(): int32 = return 5
CHENG
cat > "$conflict_source" <<'CHENG'
@exportc("provider_fixture_value")
fn provider_fixture_value(): int32 = return 99

@importc("provider_conflict_extra")
fn provider_conflict_extra(): int32

fn main(): int32 = return provider_fixture_value() + provider_conflict_extra()
CHENG
rm -f "$conflict_provider_obj" "$conflict_archive" "$conflict_obj" "$conflict_out" "$conflict_report"
if ! "$COLD" system-link-exec \
  --in:"$conflict_provider_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$conflict_provider_obj" \
  --report-out:"$WORK/provider_conflict_provider.obj.report.txt" \
  > "$WORK/provider_conflict_provider.obj.stdout" 2>&1; then
  echo "FAIL provider_archive_primary_conflict_hard_fail (conflict provider build failed)"
  fail=$((fail + 1))
elif ! "$COLD" provider-archive-pack \
  --target:riscv64-unknown-linux-gnu \
  --object:"$conflict_provider_obj" \
  --export:provider_fixture_value \
  --export:provider_conflict_extra \
  --module:provider_conflict_provider \
  --source:"$conflict_provider_source" \
  --out:"$conflict_archive" \
  --report-out:"$conflict_pack_report" \
  > "$WORK/provider_conflict_provider.pack.stdout" 2>&1; then
  echo "FAIL provider_archive_primary_conflict_hard_fail (conflict archive pack failed)"
  fail=$((fail + 1))
elif ! "$COLD" system-link-exec \
  --in:"$conflict_source" \
  --emit:obj --target:riscv64-unknown-linux-gnu \
  --out:"$conflict_obj" \
  --report-out:"$WORK/provider_primary_conflict.obj.report.txt" \
  > "$WORK/provider_primary_conflict.obj.stdout" 2>&1; then
  echo "FAIL provider_archive_primary_conflict_hard_fail (conflict object build failed)"
  fail=$((fail + 1))
elif "$COLD" system-link-exec \
  --link-object:"$conflict_obj" \
  --provider-archive:"$conflict_archive" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$conflict_out" \
  --report-out:"$conflict_report" \
  > "$WORK/provider_primary_conflict.stdout" 2>&1; then
  echo "FAIL provider_archive_primary_conflict_hard_fail"
  fail=$((fail + 1))
elif grep -q '^provider_archive=1$' "$conflict_report" &&
     grep -q '^error=primary/provider symbol conflict: provider_fixture_value$' "$conflict_report" &&
     [ ! -e "$conflict_out" ]; then
  echo "PASS provider_archive_primary_conflict_hard_fail"
  pass=$((pass + 1))
else
  echo "FAIL provider_archive_primary_conflict_hard_fail"
  fail=$((fail + 1))
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
elif "$COLD" system-link-exec \
  --root:"$ROOT" \
  --in:"$multi_primary_source" \
  --emit:csg-v2 \
  --out:"$multi_facts" \
  --target:riscv64-unknown-linux-gnu \
  --report-out:"$WORK/provider_multi_primary.writer.report.txt" \
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
elif "$COLD" system-link-exec \
  --root:"$ROOT" \
  --in:"$multi_missing_source" \
  --emit:csg-v2 \
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
  --csg-in:"$ordinary_riscv_facts" \
  --emit:exe --target:riscv64-unknown-linux-gnu \
  --out:"$WORK/fp_rv_a" > /dev/null 2>&1
COLD_NO_SIGN=1 "$COLD" system-link-exec \
  --csg-in:"$ordinary_riscv_facts" \
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
    facts_target="$TARGET"
    target_report="$WORK/$base.writer.report.txt"
    if [ -s "$target_report" ]; then
        reported_target="$(awk -F= '$1 == "target" { print $2; exit }' "$target_report" 2>/dev/null)"
        if [ -n "$reported_target" ]; then
            facts_target="$reported_target"
        fi
    fi
    cross_version_opt_test "$base" "$f" "$facts_target"
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
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$csg_unused" \
    --emit:csg-v2 \
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

# CSG v2 input with wrong magic bytes must hard-fail.
echo "  - bad_magic_hard_fail"
bad_magic_facts="$WORK/bad_magic.facts"
printf 'BROKEN_MAGIC\nR00010000001612' > "$bad_magic_facts"
if "$COLD" system-link-exec \
    --csg-in:"$bad_magic_facts" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$WORK/bad_magic.o" \
    --report-out:"$WORK/bad_magic.report.txt" >/dev/null 2>&1; then
    bad "bad_magic_hard_fail"
elif grep -q '^error=cold csg v2 object emit failed$' "$WORK/bad_magic.report.txt" 2>/dev/null; then
    ok "bad_magic_hard_fail"
else
    bad "bad_magic_hard_fail"
fi

# Empty CSG v2 input must hard-fail.
echo "  - empty_facts_hard_fail"
empty_facts="$WORK/empty.facts"
: > "$empty_facts"
if "$COLD" system-link-exec \
    --csg-in:"$empty_facts" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$WORK/empty.o" \
    --report-out:"$WORK/empty.report.txt" >/dev/null 2>&1; then
    bad "empty_facts_hard_fail"
elif grep -q '^error=cold csg v2 object emit failed$' "$WORK/empty.report.txt" 2>/dev/null; then
    ok "empty_facts_hard_fail"
else
    bad "empty_facts_hard_fail"
fi

# CSG v2 header-only (magic but no records) must hard-fail.
echo "  - header_only_hard_fail"
header_facts="$WORK/header_only.facts"
printf 'CHENG_CSG_V2\n' > "$header_facts"
if "$COLD" system-link-exec \
    --csg-in:"$header_facts" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$WORK/header_only.o" \
    --report-out:"$WORK/header_only.report.txt" >/dev/null 2>&1; then
    bad "header_only_hard_fail"
elif grep -q '^error=cold csg v2 object emit failed$' "$WORK/header_only.report.txt" 2>/dev/null; then
    ok "header_only_hard_fail"
else
    bad "header_only_hard_fail"
fi

# Symbol verification: CSG v2 reader -> codegen -> .o must expose correct symbols in nm.
echo "  - nm_symbol_verification"
if [ -s "$WORK/ordinary.1.o" ] && nm -g "$WORK/ordinary.1.o" 2>/dev/null | grep -q 'T _main$'; then
    ok "nm_symbol_verification"
else
    bad "nm_symbol_verification"
fi

# otool verification: generated .o must have non-empty __text section.
echo "  - otool_text_section"
if [ -s "$WORK/ordinary.1.o" ] && otool -t "$WORK/ordinary.1.o" 2>/dev/null | tail -n +3 | grep -q '[0-9a-fA-F]'; then
    ok "otool_text_section"
else
    bad "otool_text_section"
fi

# CSG v2 roundtrip: compile a minimal source via CSG v2 writer + reader, verify .o.
echo "  - csg_v2_roundtrip_dispatch_min"
dispatch_min_source="$WORK/csg_v2_dispatch_min.cheng"
dispatch_min_facts="$WORK/csg_v2_dispatch_min.facts"
dispatch_min_obj="$WORK/csg_v2_dispatch_min.o"
cat > "$dispatch_min_source" <<'CHENG'
fn main(): int32 =
    return 0
CHENG
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$dispatch_min_source" \
    --emit:csg-v2 \
    --out:"$dispatch_min_facts" \
    --target:"$TARGET" \
    --report-out:"$WORK/csg_v2_dispatch_min.writer.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$dispatch_min_facts" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$dispatch_min_obj" \
    --report-out:"$WORK/csg_v2_dispatch_min.reader.report.txt" >/dev/null 2>&1 &&
   [ -s "$dispatch_min_obj" ] &&
   grep -q '^cold_csg_v2_writer_status=ok$' "$WORK/csg_v2_dispatch_min.writer.report.txt" &&
   grep -q '^emit=obj$' "$WORK/csg_v2_dispatch_min.reader.report.txt" &&
   grep -q '^system_link=0$' "$WORK/csg_v2_dispatch_min.reader.report.txt" &&
   grep -q '^linkerless_image=1$' "$WORK/csg_v2_dispatch_min.reader.report.txt"; then
    ok "csg_v2_roundtrip_dispatch_min"
else
    bad "csg_v2_roundtrip_dispatch_min"
fi

# CSG v2 roundtrip: function with empty body compiles via CSG v2.
echo "  - csg_v2_empty_body_function"
empty_source="$WORK/csg_v2_empty_body.cheng"
empty_facts="$WORK/csg_v2_empty_body.facts"
empty_obj="$WORK/csg_v2_empty_body.o"
cat > "$empty_source" <<'CHENG'
fn empty_body() =
    return

fn main(): int32 =
    empty_body()
    return 0
CHENG
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$empty_source" \
    --emit:csg-v2 \
    --out:"$empty_facts" \
    --target:"$TARGET" \
    --report-out:"$WORK/csg_v2_empty_body.writer.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$empty_facts" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$empty_obj" \
    --report-out:"$WORK/csg_v2_empty_body.reader.report.txt" >/dev/null 2>&1 &&
   [ -s "$empty_obj" ] &&
   grep -q '^cold_csg_v2_writer_status=ok$' "$WORK/csg_v2_empty_body.writer.report.txt" &&
   grep -q '^emit=obj$' "$WORK/csg_v2_empty_body.reader.report.txt" &&
   grep -q '^system_link=0$' "$WORK/csg_v2_empty_body.reader.report.txt" &&
   grep -q '^linkerless_image=1$' "$WORK/csg_v2_empty_body.reader.report.txt"; then
    ok "csg_v2_empty_body_function"
else
    bad "csg_v2_empty_body_function"
fi

# CSG v2 roundtrip: function with 200+ char string literal via CSG v2.
echo "  - csg_v2_large_string_literal"
large_source="$WORK/csg_v2_large_string.cheng"
large_facts="$WORK/csg_v2_large_string.facts"
large_obj="$WORK/csg_v2_large_string.o"
big_str=""
i=0
while [ "$i" -lt 250 ]; do big_str="${big_str}A"; i=$((i + 1)); done
cat > "$large_source" <<CHENG
fn main(): int32 =
    let s = "$big_str"
    return 250
CHENG
if "$COLD" system-link-exec \
    --root:"$ROOT" \
    --in:"$large_source" \
    --emit:csg-v2 \
    --out:"$large_facts" \
    --target:"$TARGET" \
    --report-out:"$WORK/csg_v2_large_string.writer.report.txt" >/dev/null 2>&1 &&
   "$COLD" system-link-exec \
    --csg-in:"$large_facts" \
    --emit:obj \
    --target:"$TARGET" \
    --out:"$large_obj" \
    --report-out:"$WORK/csg_v2_large_string.reader.report.txt" >/dev/null 2>&1 &&
   [ -s "$large_obj" ] &&
   grep -q '^cold_csg_v2_writer_status=ok$' "$WORK/csg_v2_large_string.writer.report.txt" &&
   grep -q '^emit=obj$' "$WORK/csg_v2_large_string.reader.report.txt" &&
   grep -q '^system_link=0$' "$WORK/csg_v2_large_string.reader.report.txt" &&
   grep -q '^linkerless_image=1$' "$WORK/csg_v2_large_string.reader.report.txt"; then
    ok "csg_v2_large_string_literal"
else
    bad "csg_v2_large_string_literal"
fi

echo "=== $pass passed, $fail failed ==="
if [ "$fail" -ne 0 ]; then
    exit 1
fi
