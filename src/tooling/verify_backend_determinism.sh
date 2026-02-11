#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"


fixture="tests/cheng/backend/fixtures/return_object_fields.cheng"
out_dir="artifacts/backend_determinism"
mkdir -p "$out_dir"

out_a="$out_dir/a.o"
out_b="$out_dir/b.o"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=arm64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$out_a" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_TARGET=arm64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$out_b" \
"$driver"

cmp "$out_a" "$out_b" >/dev/null 2>&1 || {
  echo "[verify_backend_determinism] mismatch: $out_a vs $out_b" >&2
  exit 1
}

echo "verify_backend_determinism ok"
