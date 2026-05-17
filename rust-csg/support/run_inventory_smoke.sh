#!/usr/bin/env bash
set -euo pipefail

ROOT="${ROOT:-/Users/lbcheng/cheng-lang}"
OUT_DIR="${OUT_DIR:-/tmp/rust_csg_inventory_smoke_work}"

mkdir -p "$OUT_DIR"

cc -arch arm64 -O2 \
  -c "$ROOT/rust-csg/support/darwin_inventory_provider.c" \
  -o "$OUT_DIR/darwin_inventory_provider.o"

"$ROOT/artifacts/backend_driver/cheng" system-link-exec \
  --root:"$ROOT" \
  --in:"$ROOT/rust-csg/src/tests/rust_csg_inventory_smoke.cheng" \
  --emit:obj \
  --target:arm64-apple-darwin \
  --out:"$OUT_DIR/rust_csg_inventory_smoke.o" \
  --report-out:"$OUT_DIR/rust_csg_inventory_smoke.obj.report.txt"

cc -arch arm64 \
  "$OUT_DIR/rust_csg_inventory_smoke.o" \
  "$OUT_DIR/darwin_inventory_provider.o" \
  -lc \
  -o "$OUT_DIR/rust_csg_inventory_smoke"

"$OUT_DIR/rust_csg_inventory_smoke"
