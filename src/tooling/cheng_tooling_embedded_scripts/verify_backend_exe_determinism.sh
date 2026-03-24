#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
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
linker_mode="self"
target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target 2>/dev/null || echo arm64-apple-darwin)"

print_usage() {
  echo "Usage: $0 [--target:<triple>] [--linker:self|system]"
}

while [ "${1:-}" != "" ]; do
  case "$1" in
    --target:*)
      target="${1#--target:}"
      ;;
    --linker:*)
      linker_mode="${1#--linker:}"
      ;;
    --help|-h)
      print_usage
      exit 0
      ;;
    *)
      echo "[verify_backend_exe_determinism] unknown arg: $1" >&2
      print_usage >&2
      exit 2
      ;;
  esac
  shift || true
done

case "$linker_mode" in
  self|system)
    ;;
  *)
    echo "[verify_backend_exe_determinism] invalid linker: $linker_mode" >&2
    exit 2
    ;;
esac

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
if [ ! -f "$fixture" ]; then
  fixture="tests/cheng/backend/fixtures/return_i64.cheng"
fi
out_dir="artifacts/backend_exe_determinism"
mkdir -p "$out_dir"

exe_path="$out_dir/out"
runtime_env_log="$out_dir/runtime_env.log"
runtime_env_line=""
if [ "$linker_mode" = "self" ]; then
  set +e
  runtime_env_line="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_link_env --driver:"$driver" --target:"$target" --linker:self 2>"$runtime_env_log")"
  runtime_env_status="$?"
  set -e
  if [ "$runtime_env_status" -ne 0 ]; then
    echo "[verify_backend_exe_determinism] backend_link_env failed for target=$target (log: $runtime_env_log)" >&2
    exit 1
  fi
fi

run_exe() {
  if [ "$linker_mode" = "self" ]; then
    # shellcheck disable=SC2086
    env $runtime_env_line BACKEND_LDFLAGS="$ldflags" \
      "$driver" "$fixture" \
      --emit:exe \
      --target:"$target" \
      --linker:self \
      --no-multi \
      --no-multi-force \
      --output:"$exe_path"
    return
  fi
  env BACKEND_LDFLAGS="$ldflags" \
    "$driver" "$fixture" \
    --emit:exe \
    --target:"$target" \
    --linker:system \
    --no-multi \
    --no-multi-force \
    --output:"$exe_path"
}

run_exe
sha_a="$(sha256_file "$exe_path")"

run_exe
sha_b="$(sha256_file "$exe_path")"
if [ "$sha_a" = "" ] || [ "$sha_b" = "" ]; then
  echo "verify_backend_exe_determinism skip: missing sha256 tool" 1>&2
  exit 2
fi
if [ "$sha_a" != "$sha_b" ]; then
  echo "[verify_backend_exe_determinism] mismatch: $sha_a vs $sha_b" >&2
  exit 1
fi

echo "verify_backend_exe_determinism ok"
