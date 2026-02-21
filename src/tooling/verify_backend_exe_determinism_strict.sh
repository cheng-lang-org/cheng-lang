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
linker_mode="${BACKEND_LINKER:-self}"
target="${BACKEND_TARGET:-$(sh src/tooling/detect_host_target.sh 2>/dev/null || echo arm64-apple-darwin)}"

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
    echo "verify_backend_exe_determinism_strict skip: unsupported host os: $host_os" 1>&2
    exit 2
    ;;
esac


fixture="tests/cheng/backend/fixtures/return_add.cheng"
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
out_dir="artifacts/backend_exe_determinism_strict"
mkdir -p "$out_dir"
mkdir -p "$out_dir/tmp_a" "$out_dir/tmp_b" "$out_dir/tmp_c"

exe_path="$out_dir/out"
runtime_env_log="$out_dir/runtime_env.log"
runtime_env_line=""
if [ "$linker_mode" = "self" ]; then
  set +e
  runtime_env_line="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:self 2>"$runtime_env_log")"
  runtime_env_status="$?"
  set -e
  if [ "$runtime_env_status" -ne 0 ]; then
    echo "[verify_backend_exe_determinism_strict] backend_link_env failed for target=$target (log: $runtime_env_log)" >&2
    exit 1
  fi
fi

run_exe() {
  if [ "$linker_mode" = "self" ]; then
    # shellcheck disable=SC2086
    env $runtime_env_line \
      BACKEND_EMIT=exe \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_RUNTIME=off \
      BACKEND_TARGET="$target" \
      BACKEND_LDFLAGS="$ldflags" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$exe_path" \
      "$@" \
      "$driver"
  else
    env \
      BACKEND_EMIT=exe \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_LINKER=system \
      BACKEND_TARGET="$target" \
      BACKEND_LDFLAGS="$ldflags" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$exe_path" \
      "$@" \
      "$driver"
  fi
}

run_exe env LANG=C LC_ALL=C TZ=UTC TMPDIR="$out_dir/tmp_a"
sha_a="$(sha256_file "$exe_path")"

run_exe env LANG=C LC_ALL=C TZ=Asia/Shanghai TMPDIR="$out_dir/tmp_b"
sha_b="$(sha256_file "$exe_path")"

run_exe env LANG=C LC_ALL=C TZ=UTC TMPDIR="$out_dir/tmp_c" BACKEND_JOBS=1
sha_c="$(sha256_file "$exe_path")"

if [ "$sha_a" = "" ] || [ "$sha_b" = "" ] || [ "$sha_c" = "" ]; then
  echo "verify_backend_exe_determinism_strict skip: missing sha256 tool" 1>&2
  exit 2
fi
if [ "$sha_a" != "$sha_b" ] || [ "$sha_b" != "$sha_c" ]; then
  echo "[verify_backend_exe_determinism_strict] mismatch: $sha_a vs $sha_b vs $sha_c" 1>&2
  exit 1
fi

echo "verify_backend_exe_determinism_strict ok"
