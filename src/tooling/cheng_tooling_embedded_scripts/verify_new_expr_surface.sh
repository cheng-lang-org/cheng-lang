#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ] || [ "${1:-}" = "help" ]; then
  echo "Usage: sh src/tooling/cheng_tooling_embedded_scripts/verify_new_expr_surface.sh"
  echo "Verifies real Cheng sources use expression-style new(T) surface and that the selected compiler compiles and runs typed/inferred new(T) fixtures."
  exit 0
fi

if ! command -v rg >/dev/null 2>&1; then
  echo "[verify_new_expr_surface] rg is required" >&2
  exit 2
fi

stage2="${NEW_EXPR_STAGE2:-$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof/cheng.stage2}"
if [ ! -x "$stage2" ]; then
  echo "[verify_new_expr_surface] missing compiler binary: $stage2" >&2
  exit 2
fi

out_dir="$root/artifacts/new_expr_surface"
rm -rf "$out_dir"
mkdir -p "$out_dir"

residual="$out_dir/residual_new_stmt.txt"
fixture="$out_dir/new_expr_min.cheng"
binary="$out_dir/new_expr_min.bin"
compile_log="$out_dir/new_expr_min.compile.log"
run_log="$out_dir/new_expr_min.run.log"
report="$out_dir/new_expr_surface.report.txt"
compile_attempts=0
inferred_fixture="$root/tests/cheng/backend/fixtures/return_new_expr_infer_box.cheng"
generic_fixture="$root/tests/cheng/backend/fixtures/return_new_expr_infer_generic_box.cheng"
inferred_binary="$out_dir/return_new_expr_infer_box.bin"
inferred_compile_log="$out_dir/return_new_expr_infer_box.compile.log"
inferred_run_log="$out_dir/return_new_expr_infer_box.run.log"
generic_binary="$out_dir/return_new_expr_infer_generic_box.bin"
generic_compile_log="$out_dir/return_new_expr_infer_generic_box.compile.log"
generic_run_log="$out_dir/return_new_expr_infer_generic_box.run.log"
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
compat_report="$root/artifacts/new_expr_stage0_compat/new_expr_stage0_compat.report.txt"
compat_compile_log=""
compat_run_log=""
compat_rewrite=""
compat_stage0_enabled=0
compat_compile_rc="skip"
compat_run_rc="skip"

if rg -n '^[[:space:]]*new[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*$' \
  src --glob '*.cheng' --glob '!**/cheng_sidecar_rewrite_*.cheng' >"$residual"; then
  echo "[verify_new_expr_surface] legacy statement-style new surface remains in real source" >&2
  sed -n '1,120p' "$residual" >&2 || true
  exit 1
fi

cat >"$fixture" <<'EOF'
type
    Box = ref
        x: int32

fn main(): int32 =
    var b: Box = new(Box)
    b.x = 7
    return b.x
EOF

run_fixture() {
  fixture_path="$1"
  binary_path="$2"
  compile_path="$3"
  run_path="$4"
  expect_rc="$5"
  label="$6"
  attempts=0
  rc=1
  while :; do
    attempts=$((attempts + 1))
    rm -f "$binary_path" "$binary_path.tmp.linkobj" "$binary_path.tmp.linkobj.c" "$binary_path.tmp.linkobj.o" "$binary_path.tmp.linkobj.patched.o"
    set +e
    "$stage2" "$fixture_path" --output:"$binary_path" >"$compile_path" 2>&1
    rc=$?
    set -e
    if [ "$rc" -eq 0 ]; then
      break
    fi
    if [ "$attempts" -ge 3 ]; then
      echo "[verify_new_expr_surface] $label compile failed rc=$rc after $attempts attempts" >&2
      sed -n '1,160p' "$compile_path" >&2 || true
      exit 1
    fi
  done

  set +e
  "$binary_path" >"$run_path" 2>&1
  run_rc_local=$?
  set -e
  if [ "$run_rc_local" -ne "$expect_rc" ]; then
    echo "[verify_new_expr_surface] $label returned unexpected rc=$run_rc_local" >&2
    sed -n '1,120p' "$compile_path" >&2 || true
    sed -n '1,120p' "$run_path" >&2 || true
    exit 1
  fi
}

run_fixture "$fixture" "$binary" "$compile_log" "$run_log" 7 "typed minimal new(T) fixture"
compile_attempts=3
compile_rc=0
run_rc=7
if [ -x "$compat_stage0" ] && [ -x "$compat_driver" ]; then
  compat_stage0_enabled=1
  env \
    NEW_EXPR_COMPAT_STAGE0="$compat_stage0" \
    NEW_EXPR_COMPAT_DRIVER="$compat_driver" \
    NEW_EXPR_COMPAT_TARGET="$compat_target" \
    /bin/sh "$root/src/tooling/cheng_tooling_embedded_scripts/verify_new_expr_stage0_compat.sh" >/dev/null
  if [ ! -f "$compat_report" ]; then
    echo "[verify_new_expr_surface] compat report missing: $compat_report" >&2
    exit 1
  fi
  compat_compile_log="$(sed -n 's/^compile_log=//p' "$compat_report" | tail -n 1)"
  compat_run_log="$(sed -n 's/^run_log=//p' "$compat_report" | tail -n 1)"
  compat_rewrite="$(sed -n 's/^rewrite_path=//p' "$compat_report" | tail -n 1)"
  compat_compile_rc="$(sed -n 's/^compile_rc=//p' "$compat_report" | tail -n 1)"
  compat_run_rc="$(sed -n 's/^run_rc=//p' "$compat_report" | tail -n 1)"
fi
run_fixture "$inferred_fixture" "$inferred_binary" "$inferred_compile_log" "$inferred_run_log" 0 "inferred new(Box) fixture"
run_fixture "$generic_fixture" "$generic_binary" "$generic_compile_log" "$generic_run_log" 0 "inferred new(Box[T]) fixture"

{
  echo "status=ok"
  echo "stage2=$stage2"
  echo "residual_scan=$residual"
  echo "fixture=$fixture"
  echo "binary=$binary"
  echo "compile_log=$compile_log"
  echo "run_log=$run_log"
  echo "inferred_fixture=$inferred_fixture"
  echo "inferred_binary=$inferred_binary"
  echo "inferred_compile_log=$inferred_compile_log"
  echo "inferred_run_log=$inferred_run_log"
  echo "generic_fixture=$generic_fixture"
  echo "generic_binary=$generic_binary"
  echo "generic_compile_log=$generic_compile_log"
  echo "generic_run_log=$generic_run_log"
  echo "compat_stage0=$compat_stage0"
  echo "compat_driver=$compat_driver"
  echo "compat_target=$compat_target"
  echo "compat_report=$compat_report"
  echo "compat_stage0_enabled=$compat_stage0_enabled"
  echo "compat_compile_log=$compat_compile_log"
  echo "compat_run_log=$compat_run_log"
  echo "compat_rewrite=$compat_rewrite"
  echo "compat_compile_rc=$compat_compile_rc"
  echo "compat_run_rc=$compat_run_rc"
  echo "compile_attempts=$compile_attempts"
  echo "compile_rc=$compile_rc"
  echo "run_rc=$run_rc"
} >"$report"

echo "verify_new_expr_surface ok"
