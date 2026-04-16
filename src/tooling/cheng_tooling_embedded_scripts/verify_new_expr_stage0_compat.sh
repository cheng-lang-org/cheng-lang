#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "help" ]; then
  echo "Usage: sh src/tooling/cheng_tooling_embedded_scripts/verify_new_expr_stage0_compat.sh"
  echo "Verifies old stage0 compilers can compile expression-style new(Type) via canonical sidecar rewrite."
  exit 0
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_new_expr_stage0_compat] rg is required" >&2
  exit 2
fi

compat_stage0_default="$root/dist/releases/2026-02-23T09_54_03Z_e84f22d_14/cheng"
compat_stage0="${NEW_EXPR_COMPAT_STAGE0:-$compat_stage0_default}"
compat_driver="${NEW_EXPR_COMPAT_DRIVER:-}"
if [ "$compat_driver" = "" ]; then
  compat_driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path 2>/dev/null || true)"
fi
if [ "$compat_driver" = "" ]; then
  compat_driver="$root/artifacts/v3_backend_driver/cheng"
fi
compat_target="${NEW_EXPR_COMPAT_TARGET:-arm64-apple-darwin}"

if [ ! -x "$compat_stage0" ]; then
  echo "[verify_new_expr_stage0_compat] missing compat stage0: $compat_stage0" >&2
  exit 2
fi
if [ ! -x "$compat_driver" ]; then
  echo "[verify_new_expr_stage0_compat] missing compat driver: $compat_driver" >&2
  exit 2
fi

out_dir="$root/artifacts/new_expr_stage0_compat"
rm -rf "$out_dir"
mkdir -p "$out_dir"

fixture="$out_dir/new_expr_stage0_compat_min.cheng"
binary="$out_dir/new_expr_stage0_compat_min.bin"
compile_log="$out_dir/new_expr_stage0_compat_min.compile.log"
run_log="$out_dir/new_expr_stage0_compat_min.run.log"
report="$out_dir/new_expr_stage0_compat.report.txt"

cat >"$fixture" <<'EOF'
type
    Box = ref
        x: int32

fn main(): int32 =
    var b: Box = new(Box)
    b.x = 7
    return b.x
EOF

rm -f "$binary" "$binary.tmp.linkobj" "$binary.tmp.linkobj.c" \
  "$binary.tmp.linkobj.o" "$binary.tmp.linkobj.patched.o"

set +e
env \
  BACKEND_UIR_SIDECAR_COMPILER="$compat_stage0" \
  BACKEND_DEBUG_SIDECAR_KEEP_REWRITE=1 \
  "$compat_driver" \
  --input:"$fixture" \
  --output:"$binary" \
  --target:"$compat_target" \
  --linker:self >"$compile_log" 2>&1
compile_rc=$?
set -e
if [ "$compile_rc" -ne 0 ]; then
  echo "[verify_new_expr_stage0_compat] compile failed rc=$compile_rc" >&2
  sed -n '1,160p' "$compile_log" >&2 || true
  exit 1
fi

rewrite_path="$(sed -n "s/.*rewrite_input='\\([^']*\\)'.*/\\1/p" "$compile_log" | tail -n 1)"
if [ "$rewrite_path" = "" ] || [ ! -f "$rewrite_path" ]; then
  echo "[verify_new_expr_stage0_compat] rewrite path missing" >&2
  sed -n '1,160p' "$compile_log" >&2 || true
  exit 1
fi
if ! rg -q '__cheng_new_tmp_' "$rewrite_path"; then
  echo "[verify_new_expr_stage0_compat] rewrite did not materialize temp local new()" >&2
  sed -n '1,120p' "$rewrite_path" >&2 || true
  exit 1
fi
if rg -q 'new expects local var ref' "$compile_log"; then
  echo "[verify_new_expr_stage0_compat] regressed to legacy new(Type) builder failure" >&2
  sed -n '1,160p' "$compile_log" >&2 || true
  exit 1
fi

set +e
"$binary" >"$run_log" 2>&1
run_rc=$?
set -e
if [ "$run_rc" -ne 7 ]; then
  echo "[verify_new_expr_stage0_compat] binary returned unexpected rc=$run_rc" >&2
  sed -n '1,160p' "$compile_log" >&2 || true
  sed -n '1,120p' "$run_log" >&2 || true
  exit 1
fi

{
  echo "status=ok"
  echo "compat_stage0=$compat_stage0"
  echo "compat_driver=$compat_driver"
  echo "compat_target=$compat_target"
  echo "fixture=$fixture"
  echo "binary=$binary"
  echo "compile_log=$compile_log"
  echo "run_log=$run_log"
  echo "rewrite_path=$rewrite_path"
  echo "compile_rc=$compile_rc"
  echo "run_rc=$run_rc"
} >"$report"

echo "verify_new_expr_stage0_compat ok"
