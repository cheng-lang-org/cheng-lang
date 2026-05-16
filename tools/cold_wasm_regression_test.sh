#!/bin/bash
# WASM compilation regression test.
# Verifies that the cold compiler produces valid .wasm binaries.
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
        --root:"$ROOT" \
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

# 9. Verify expected exports in each wasm binary
for wasm in "$WORK"/*.wasm; do
    base="$(basename "$wasm" .wasm)"
    case "$base" in
        func_block)
            # func_block_smoke exports "constantSeven" not "main"
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

# 10. Verify specific known byte patterns for well-formed modules
for wasm in "$WORK"/*.wasm; do
    base="$(basename "$wasm" .wasm)"
    # Magic bytes: \0asm = 00 61 73 6d
    magic_ok=0
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

echo "=== $pass passed, $fail failed ==="
exit $fail
