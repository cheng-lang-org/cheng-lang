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
  if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$1" | awk '{print $1}'
    return
  fi
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | awk '{print $1}'
    return
  fi
  echo ""
}

host_os="$(uname -s 2>/dev/null || echo unknown)"
case "$host_os" in
  Darwin)
    ;;
  *)
    echo "verify_backend_obj_determinism_strict skip: unsupported host os: $host_os" 1>&2
    exit 2
    ;;
esac


fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
out_dir="artifacts/backend_obj_determinism_strict"
mkdir -p "$out_dir"
mkdir -p "$out_dir/tmp_a" "$out_dir/tmp_b" "$out_dir/tmp_c"

obj_a="$out_dir/a.o"
obj_b="$out_dir/b.o"
obj_c="$out_dir/c.o"

run_obj() {
  out="$1"
  shift || true
  BACKEND_EMIT=obj \
  BACKEND_TARGET=arm64-apple-darwin \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$out" \
  "$@" \
  "$driver"
}

run_obj "$obj_a" env LANG=C LC_ALL=C TZ=UTC TMPDIR="$out_dir/tmp_a"
run_obj "$obj_b" env LANG=C LC_ALL=C TZ=Asia/Shanghai TMPDIR="$out_dir/tmp_b"
run_obj "$obj_c" env LANG=C LC_ALL=C TZ=UTC TMPDIR="$out_dir/tmp_c" BACKEND_JOBS=1

sha_a="$(sha256_file "$obj_a")"
sha_b="$(sha256_file "$obj_b")"
sha_c="$(sha256_file "$obj_c")"
if [ "$sha_a" = "" ] || [ "$sha_b" = "" ] || [ "$sha_c" = "" ]; then
  echo "verify_backend_obj_determinism_strict skip: missing sha256 tool" 1>&2
  exit 2
fi
if [ "$sha_a" != "$sha_b" ] || [ "$sha_b" != "$sha_c" ]; then
  echo "[verify_backend_obj_determinism_strict] mismatch: $sha_a vs $sha_b vs $sha_c" 1>&2
  exit 1
fi

echo "verify_backend_obj_determinism_strict ok"
