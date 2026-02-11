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
linker_mode="${CHENG_BACKEND_LINKER:-self}"
target="${CHENG_BACKEND_TARGET:-arm64-apple-darwin}"
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="${CHENG_BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"

if [ "$linker_mode" = "self" ]; then
  mkdir -p chengcache
  if [ ! -f "$runtime_obj" ] || [ "src/std/system_helpers_backend.cheng" -nt "$runtime_obj" ]; then
    env \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET="$target" \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
      CHENG_BACKEND_OUTPUT="$runtime_obj" \
      "$driver" >/dev/null
  fi
fi


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
ldflags=""
case "$host_os" in
  Darwin)
    ldflags="-Wl,-no_uuid"
    ;;
  Linux)
    ldflags="-Wl,--build-id=none"
    ;;
  *)
    echo "verify_backend_exe_determinism skip: unsupported host os: $host_os" 1>&2
    exit 2
    ;;
esac

fixture="tests/cheng/backend/fixtures/return_add.cheng"
out_dir="artifacts/backend_exe_determinism"
mkdir -p "$out_dir"

exe_path="$out_dir/out"

if [ "$linker_mode" = "self" ]; then
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_LDFLAGS="$ldflags" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
else
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_LINKER=system \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_LDFLAGS="$ldflags" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
fi

sha_a="$(sha256_file "$exe_path")"

if [ "$linker_mode" = "self" ]; then
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_LDFLAGS="$ldflags" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
else
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_LINKER=system \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_LDFLAGS="$ldflags" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
fi

sha_b="$(sha256_file "$exe_path")"
if [ "$sha_a" = "" ] || [ "$sha_b" = "" ]; then
  echo "verify_backend_exe_determinism skip: missing sha256 tool" 1>&2
  exit 2
fi
if [ "$sha_a" != "$sha_b" ]; then
  echo "[verify_backend_exe_determinism] mismatch: $sha_a vs $sha_b" 1>&2
  exit 1
fi

echo "verify_backend_exe_determinism ok"
