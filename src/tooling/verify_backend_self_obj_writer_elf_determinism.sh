#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

if [ "${CLEAN_CHENG_LOCAL:-1}" = "1" ] && [ "${TOOLING_CLEANUP_DEPTH:-0}" = "0" ]; then
  export TOOLING_CLEANUP_DEPTH=1
  cleanup_backend_driver_on_exit() {
    status=$?
    set +e
    sh src/tooling/cleanup_cheng_local.sh
    exit "$status"
  }
  trap cleanup_backend_driver_on_exit EXIT
fi

driver="$(sh src/tooling/backend_driver_path.sh)"

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
  echo "[verify_backend_self_obj_writer_elf_determinism] missing sha256 tool" >&2
  return 2
}


out_dir="artifacts/backend_self_obj_elf_determinism"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_a="$out_dir/hello_importc_puts.self.elf.a.o"
obj_b="$out_dir/hello_importc_puts.self.elf.b.o"
obj_x64_a="$out_dir/hello_importc_puts.self.elf.x86_64.a.o"
obj_x64_b="$out_dir/hello_importc_puts.self.elf.x86_64.b.o"

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=elf \
BACKEND_TARGET=aarch64-linux-android \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_a" \
"$driver"

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=elf \
BACKEND_TARGET=aarch64-linux-android \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_b" \
"$driver"

if [ ! -s "$obj_a" ] || [ ! -s "$obj_b" ]; then
  echo "[verify_backend_self_obj_writer_elf_determinism] missing outputs" >&2
  exit 1
fi

sha_a="$(sha256_file "$obj_a" || true)"
sha_b="$(sha256_file "$obj_b" || true)"
if [ -z "$sha_a" ] || [ -z "$sha_b" ]; then
  exit 2
fi
if [ "$sha_a" != "$sha_b" ]; then
  echo "[verify_backend_self_obj_writer_elf_determinism] mismatch: $sha_a != $sha_b" >&2
  exit 1
fi

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=elf \
BACKEND_TARGET=x86_64-unknown-linux-gnu \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_x64_a" \
"$driver"

BACKEND_EMIT=obj \
BACKEND_OBJ_WRITER=elf \
BACKEND_TARGET=x86_64-unknown-linux-gnu \
BACKEND_INPUT="$fixture" \
BACKEND_OUTPUT="$obj_x64_b" \
"$driver"

if [ ! -s "$obj_x64_a" ] || [ ! -s "$obj_x64_b" ]; then
  echo "[verify_backend_self_obj_writer_elf_determinism] missing x86_64 outputs" >&2
  exit 1
fi

sha_x64_a="$(sha256_file "$obj_x64_a" || true)"
sha_x64_b="$(sha256_file "$obj_x64_b" || true)"
if [ -z "$sha_x64_a" ] || [ -z "$sha_x64_b" ]; then
  exit 2
fi
if [ "$sha_x64_a" != "$sha_x64_b" ]; then
  echo "[verify_backend_self_obj_writer_elf_determinism] x86_64 mismatch: $sha_x64_a != $sha_x64_b" >&2
  exit 1
fi

echo "verify_backend_self_obj_writer_elf_determinism ok"
