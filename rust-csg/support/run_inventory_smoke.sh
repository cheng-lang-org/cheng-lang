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
  --emit:exe \
  --target:arm64-apple-darwin \
  --provider-objects:"$OUT_DIR/darwin_inventory_provider.o" \
  --out:"$OUT_DIR/rust_csg_inventory_smoke" \
  --report-out:"$OUT_DIR/rust_csg_inventory_smoke.report.txt"

rg -q '^system_link_exec_scope=cold_macho_provider_system_link$' \
  "$OUT_DIR/rust_csg_inventory_smoke.report.txt"
rg -q '^provider_object_count=1$' \
  "$OUT_DIR/rust_csg_inventory_smoke.report.txt"

"$OUT_DIR/rust_csg_inventory_smoke"
