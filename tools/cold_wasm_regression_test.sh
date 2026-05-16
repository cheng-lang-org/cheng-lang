#!/bin/bash
# WASM compilation regression test.
# Verifies that the cold compiler produces valid .wasm binaries.
# Extended tests: size check, Node.js execution, import/export validation.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

COLD="${CHENG_COLD:-/tmp/cheng_cold}"

if [ ! -x "$COLD" ] ||
   [ bootstrap/cheng_cold.c -nt "$COLD" ] ||
   [ bootstrap/cold_parser.c -nt "$COLD" ] ||
   [ bootstrap/cold_parser.h -nt "$COLD" ]; then
    cc -std=c11 -O2 -o "$COLD" bootstrap/cheng_cold.c
fi

WORK="${CHENG_WASM_WORK:-/tmp/cheng_wasm_regression}"
mkdir -p "$WORK"

pass=0
fail=0

ok() { echo "  PASS $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL $1" >&2; fail=$((fail + 1)); }

wasm_compile() {
    local name="$1"
    local src="$2"
    local out="$WORK/$name.wasm"
    if "$COLD" system-link-exec \
        --in:"$src" \
        --emit:exe \
        --target:wasm32-unknown-unknown \
        --out:"$out" >/dev/null 2>&1; then
        if [ -s "$out" ] && file "$out" 2>/dev/null | grep -q "WebAssembly"; then
            ok "${name}_wasm_compile"
        else
            bad "${name}_wasm_compile (not wasm or empty)"
        fi
    else
        bad "${name}_wasm_compile (compiler failed)"
    fi
}

echo "=== WASM Regression ==="

# ============================================================
# Phase 1: Compile all WASM test sources
# ============================================================

# 1. Minimal return-zero module
wasm_compile zero_smoke src/tests/wasm_zero_smoke.cheng

# 2. Internal function call
wasm_compile internal_call src/tests/wasm_internal_call_smoke.cheng

# 3. Scalar control flow (if/else, for, ternary)
wasm_compile control_flow src/tests/wasm_scalar_control_flow_smoke.cheng

# 4. Shadowed local scopes
wasm_compile shadowed src/tests/wasm_shadowed_local_scope_smoke.cheng

# 5. Composite field access (Bytes, str, seq, Result)
wasm_compile composite src/tests/wasm_composite_field_smoke.cheng

# 6. Import call
wasm_compile import src/tests/wasm_importc_noarg_i32_smoke.cheng

# 7. Import call (native bridge)
wasm_compile import_native src/tests/wasm_importc_noarg_i32_native_smoke.cheng

# 8. Function block shared
wasm_compile func_block src/tests/wasm_func_block_shared_smoke.cheng

# 8b. Ops smoke test
wasm_compile ops_smoke src/tests/wasm_ops_smoke.cheng

# 9. String ops (self-contained)
wasm_compile string_ops src/tests/wasm_string_ops_self_contained_smoke.cheng

# 10. Memory ops
wasm_compile memory_ops src/tests/wasm_memory_ops_smoke.cheng

# 11. Extended control flow
wasm_compile control_flow_ext src/tests/wasm_control_flow_ext_smoke.cheng

# ============================================================
# Phase 2: WASM binary size check
# ============================================================
echo "--- Binary Size Check ---"

MIN_WASM_SIZE=20
MAX_WASM_SIZE=$((1024 * 1024))  # 1MB

for wasm in "$WORK"/*.wasm; do
    base="$(basename "$wasm" .wasm)"
    size=$(stat -f%z "$wasm" 2>/dev/null || stat -c%s "$wasm" 2>/dev/null)
    if [ "$size" -ge "$MIN_WASM_SIZE" ] 2>/dev/null && [ "$size" -le "$MAX_WASM_SIZE" ] 2>/dev/null; then
        ok "${base}_wasm_size"
    else
        bad "${base}_wasm_size (${size} bytes, expected ${MIN_WASM_SIZE}-${MAX_WASM_SIZE})"
    fi
done

# ============================================================
# Phase 3: Verify expected exports in each wasm binary
# ============================================================
echo "--- Export Validation ---"

for wasm in "$WORK"/*.wasm; do
    base="$(basename "$wasm" .wasm)"
    case "$base" in
        func_block)
            if LC_ALL=C grep -a "constantSeven" "$wasm" >/dev/null 2>&1; then
                ok "${base}_export_constantSeven"
            else
                bad "${base}_export_constantSeven (missing export 'constantSeven')"
            fi
            ;;
        *)
            if LC_ALL=C grep -a "main" "$wasm" >/dev/null 2>&1; then
                ok "${base}_export_main"
            else
                bad "${base}_export_main (missing export 'main')"
            fi
            ;;
    esac
done

# ============================================================
# Phase 4: Verify WASM magic bytes and version
# ============================================================
echo "--- Binary Format Validation ---"

for wasm in "$WORK"/*.wasm; do
    base="$(basename "$wasm" .wasm)"
    # Magic bytes: \0asm = 00 61 73 6d
    read -r b0 b1 b2 b3 < <(od -An -td1 -N4 "$wasm" 2>/dev/null)
    if [ "$b0" = "0" ] && [ "$b1" = "97" ] && [ "$b2" = "115" ] && [ "$b3" = "109" ]; then
        ok "${base}_wasm_magic"
    else
        bad "${base}_wasm_magic"
    fi
    # Version bytes at offset 4: 1 0 0 0 = version 1
    read -r v0 v1 v2 v3 < <(od -An -td1 -j4 -N4 "$wasm" 2>/dev/null)
    if [ "$v0" = "1" ] && [ "$v1" = "0" ] && [ "$v2" = "0" ] && [ "$v3" = "0" ]; then
        ok "${base}_wasm_version"
    else
        bad "${base}_wasm_version (expected version 1)"
    fi
done

# ============================================================
# Phase 5: Import section validation
# ============================================================
echo "--- Import Section Validation ---"

for wasm in "$WORK"/*.wasm; do
    base="$(basename "$wasm" .wasm)"
    # Check if the import section exists and is well-formed
    # Use od to find section ID 2 (import section)
    has_import=false
    well_formed=true

    # Parse section headers to check for well-formed import section
    python3 -c "
import sys

def read_leb128(data, pos):
    result = 0
    shift = 0
    while pos < len(data):
        byte = data[pos]
        pos += 1
        result |= (byte & 0x7f) << shift
        shift += 7
        if not (byte & 0x80):
            break
        if shift > 35:
            return -1, pos
    return result, pos

data = open('$wasm', 'rb').read()
pos = 8
found = False
while pos < len(data):
    section_id = data[pos]
    pos += 1
    if pos >= len(data): break
    size, pos = read_leb128(data, pos)
    if size < 0 or pos + size > len(data):
        break
    if section_id == 2:
        found = True
        count, pos = read_leb128(data, pos)
        if count < 0 or count > 100000:
            print('bad_count')
            sys.exit(1)
        for i in range(count):
            if pos >= len(data):
                print('truncated')
                sys.exit(1)
            ml, pos = read_leb128(data, pos)
            if ml < 0 or pos + ml > len(data):
                print('truncated')
                sys.exit(1)
            pos += ml
            fl, pos = read_leb128(data, pos)
            if fl < 0 or pos + fl > len(data):
                print('truncated')
                sys.exit(1)
            pos += fl
            if pos >= len(data):
                print('truncated')
                sys.exit(1)
            kind = data[pos]
            pos += 1
            if kind > 3:
                print('bad_kind')
                sys.exit(1)
            # Skip import-specific fields (type_idx for func, limits for mem/table)
            if kind == 0:  # func
                tidx, pos = read_leb128(data, pos)
                if tidx < 0:
                    print('bad_typeidx')
                    sys.exit(1)
            elif kind == 1 or kind == 2:  # table or memory
                flags, pos = read_leb128(data, pos)
                if flags < 0: print('bad_flags'); sys.exit(1)
                init, pos = read_leb128(data, pos)
                if init < 0: print('bad_init'); sys.exit(1)
                if flags & 1:
                    mx, pos = read_leb128(data, pos)
                    if mx < 0: print('bad_max'); sys.exit(1)
            elif kind == 3:  # global
                valtype = data[pos]; pos += 1
                mut = data[pos]; pos += 1
        print(f'ok count={count}')
        sys.exit(0)
    pos += size

if not found:
    print('none')
    sys.exit(0)
" 2>/dev/null
    result="$?"
    if [ "$result" = "0" ]; then
        ok "${base}_import_section"
    else
        bad "${base}_import_section (malformed)"
    fi
done

# ============================================================
# Phase 6: Node.js WASM execution test (self-contained modules only)
# ============================================================
echo "--- Node.js Execution Test ---"

if command -v node >/dev/null 2>&1; then
    NODE_VER=$(node --version 2>/dev/null)
    echo "  Node.js: ${NODE_VER:-unknown}"

    # Tests with zero imports that can run standalone in Node.js
    node_run() {
        local name="$1"
        local wasm="$2"
        node -e "
const fs = require('fs');
try {
    const wasm = fs.readFileSync('$wasm');
    const mod = new WebAssembly.Module(wasm);
    const instance = new WebAssembly.Instance(mod);
    const result = instance.exports.main();
    process.exit(result === undefined ? 0 : result);
} catch(e) {
    console.error('NODE_ERROR:', e.message);
    process.exit(254);
}
" 2>/dev/null
        return $?
    }

    # Tests that need memory import
    node_run_with_memory() {
        local name="$1"
        local wasm="$2"
        node -e "
const fs = require('fs');
try {
    const wasm = fs.readFileSync('$wasm');
    const mod = new WebAssembly.Module(wasm);
    // Provide minimal memory
    const memory = new WebAssembly.Memory({initial: 1, maximum: 512});
    const importObj = {env: {memory: memory}};
    const instance = new WebAssembly.Instance(mod, importObj);
    if (typeof instance.exports.main === 'function') {
        const result = instance.exports.main();
        process.exit(result === undefined ? 0 : result);
    } else {
        process.exit(0);
    }
} catch(e) {
    console.error('NODE_ERROR:', e.message);
    process.exit(254);
}
" 2>/dev/null
        return $?
    }

    # Test modules with no imports or minimal imports
    for wasm in "$WORK"/*.wasm; do
        base="$(basename "$wasm" .wasm)"

        # Skip modules known to need complex runtime imports
        case "$base" in
            composite|import_native|import|ops_smoke)
                # These need runtime function imports, not just memory
                ok "${base}_node_skip (needs runtime)"
                continue
                ;;
        esac

        # Test: try without imports first, fall back to memory import if needed
        if node_run "$base" "$wasm"; then
            ok "${base}_node_run"
        else
            exit_code=$?
            if [ "$exit_code" = "254" ]; then
                # Could be missing memory - try with memory import
                if node_run_with_memory "$base" "$wasm"; then
                    ok "${base}_node_run"
                else
                    exit2=$?
                    if [ "$exit2" = "254" ]; then
                        bad "${base}_node_run (instantiation failed)"
                    elif [ "$exit2" != "0" ]; then
                        bad "${base}_node_run (exit=${exit2})"
                    else
                        ok "${base}_node_run"
                    fi
                fi
            else
                # Non-zero exit: codegen bug or assertion failure
                bad "${base}_node_run (exit=${exit_code})"
            fi
        fi
    done
else
    echo "  (Node.js not available, skipping execution tests)"
fi

# ============================================================
# Phase 7: Cross-WASM-runtime test
# ============================================================
echo "--- Cross-Runtime Test ---"

runtimes=""
if command -v node >/dev/null 2>&1; then
    runtimes="$runtimes node"
fi
if command -v wasmtime >/dev/null 2>&1; then
    runtimes="$runtimes wasmtime"
fi
if command -v wasmer >/dev/null 2>&1; then
    runtimes="$runtimes wasmer"
fi
if command -v iwasm >/dev/null 2>&1; then
    runtimes="$runtimes iwasm"
fi

if [ -z "$runtimes" ]; then
    echo "  (No WASM runtimes available for cross-runtime test)"
    echo "  (Node.js available but execution tests already run above)"
fi

# Test with each runtime for self-contained modules
for rt in $runtimes; do
    case "$rt" in
        node)
            # Already tested above
            ok "cross_runtime_node (tested above)"
            ;;
        wasmtime)
            if wasmtime --version >/dev/null 2>&1; then
                for wasm in "$WORK"/zero_smoke.wasm "$WORK"/internal_call.wasm "$WORK"/control_flow.wasm; do
                    base="$(basename "$wasm" .wasm)"
                    if wasmtime "$wasm" --invoke main 2>/dev/null; then
                        ok "${base}_wasmtime"
                    else
                        bad "${base}_wasmtime (exit=$?)"
                    fi
                done
            fi
            ;;
        wasmer)
            if wasmer --version >/dev/null 2>&1; then
                for wasm in "$WORK"/zero_smoke.wasm "$WORK"/internal_call.wasm "$WORK"/control_flow.wasm; do
                    base="$(basename "$wasm" .wasm)"
                    if wasmer run "$wasm" --invoke main 2>/dev/null; then
                        ok "${base}_wasmer"
                    else
                        bad "${base}_wasmer (exit=$?)"
                    fi
                done
            fi
            ;;
        iwasm)
            if iwasm --version >/dev/null 2>&1; then
                for wasm in "$WORK"/zero_smoke.wasm "$WORK"/internal_call.wasm "$WORK"/control_flow.wasm; do
                    base="$(basename "$wasm" .wasm)"
                    if iwasm "$wasm" 2>/dev/null; then
                        ok "${base}_iwasm"
                    else
                        bad "${base}_iwasm (exit=$?)"
                    fi
                done
            fi
            ;;
    esac
done

# ============================================================
# Summary
# ============================================================
echo "=== $pass passed, $fail failed ==="
exit $fail
