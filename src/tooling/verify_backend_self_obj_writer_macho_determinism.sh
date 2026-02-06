#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CHENG_CLEAN_BACKEND_MVP_DRIVER_LOCAL:-1}" = "1" ] && [ "${CHENG_TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export CHENG_TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_backend_mvp_driver_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"

if [ "$(uname -s 2>/dev/null || true)" != "Darwin" ]; then
  echo "[verify_backend_self_obj_writer_macho_determinism] non-Darwin host" >&2
  exit 2
fi

sha256_file() {
  f="$1"
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$f" | cut -d ' ' -f1
    return 0
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$f" | cut -d ' ' -f1
    return 0
  fi
  if command -v openssl >/dev/null 2>&1; then
    openssl dgst -sha256 "$f" | awk '{print $2}'
    return 0
  fi
  echo "[verify_backend_self_obj_writer_macho_determinism] missing sha256 tool" >&2
  return 2
}


out_dir="artifacts/backend_self_obj_macho_determinism"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_a="$out_dir/hello_importc_puts.self.macho.a.o"
obj_b="$out_dir/hello_importc_puts.self.macho.b.o"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_OBJ_WRITER=macho \
CHENG_BACKEND_TARGET=arm64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$obj_a" \
"$driver"

CHENG_BACKEND_EMIT=obj \
CHENG_BACKEND_OBJ_WRITER=macho \
CHENG_BACKEND_TARGET=arm64-apple-darwin \
CHENG_BACKEND_INPUT="$fixture" \
CHENG_BACKEND_OUTPUT="$obj_b" \
"$driver"

if [ ! -s "$obj_a" ] || [ ! -s "$obj_b" ]; then
  echo "[verify_backend_self_obj_writer_macho_determinism] missing outputs" >&2
  exit 1
fi

sha_a="$(sha256_file "$obj_a" || true)"
sha_b="$(sha256_file "$obj_b" || true)"
if [ -z "$sha_a" ] || [ -z "$sha_b" ]; then
  exit 2
fi
if [ "$sha_a" != "$sha_b" ]; then
  echo "[verify_backend_self_obj_writer_macho_determinism] mismatch: $sha_a != $sha_b" >&2
  exit 1
fi

echo "verify_backend_self_obj_writer_macho_determinism ok"
