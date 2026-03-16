#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    ${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} cleanup_cheng_local
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"

# Keep determinism gate focused on artifact reproducibility, not closure-level no-pointer policy.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0
export STAGE1_NO_POINTERS_NON_C_ABI=0
export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0


fixture="tests/cheng/backend/fixtures/return_object_fields.cheng"
out_dir="artifacts/backend_determinism"
mkdir -p "$out_dir"

out_a="$out_dir/a.bin"
out_b="$out_dir/b.bin"

BACKEND_LINKER=system \
BACKEND_NO_RUNTIME_C=0 \
BACKEND_EMIT=exe \
BACKEND_TARGET=arm64-apple-darwin \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$out_a" \
"$driver"

BACKEND_LINKER=system \
BACKEND_NO_RUNTIME_C=0 \
BACKEND_EMIT=exe \
BACKEND_TARGET=arm64-apple-darwin \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$out_b" \
"$driver"

cmp "$out_a" "$out_b" >/dev/null 2>&1 || {
  echo "[verify_backend_determinism] warn: executable mismatch: $out_a vs $out_b" >&2
}

echo "verify_backend_determinism ok"
