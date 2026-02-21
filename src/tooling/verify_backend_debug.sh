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

host_os="$(uname -s 2>/dev/null || echo unknown)"
if [ "$host_os" != "Darwin" ]; then
  echo "verify_backend_debug skip: darwin-only (host_os=$host_os)" 1>&2
  exit 2
fi
if ! command -v dsymutil >/dev/null 2>&1; then
  echo "verify_backend_debug skip: missing dsymutil" 1>&2
  exit 2
fi
if ! command -v nm >/dev/null 2>&1; then
  echo "verify_backend_debug skip: missing nm" 1>&2
  exit 2
fi

out_dir="artifacts/backend_debug"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/hello_puts.cheng"

build_and_check_dsym() {
  target="$1"
  exe="$2"
  dsym="$exe.dSYM"
  link_env="$(sh src/tooling/backend_link_env.sh --driver:"$driver" --target:"$target" --linker:"${BACKEND_LINKER:-auto}")"
  multi="${BACKEND_MULTI:-0}"
  multi_force="${BACKEND_MULTI_FORCE:-$multi}"

  rm -rf "$exe" "$dsym"

  set +e
  env $link_env \
    BACKEND_MULTI="$multi" \
    BACKEND_MULTI_FORCE="$multi_force" \
    BACKEND_VALIDATE=1 \
    BACKEND_EMIT=exe \
    BACKEND_TARGET="$target" \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe" \
    "$driver"
  status="$?"
  set -e
  if [ "$status" -ne 0 ] && [ "$multi" != "0" ]; then
    echo "[Warn] verify_backend_debug parallel compile failed, retry serial (target=$target)" 1>&2
    env $link_env \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_VALIDATE=1 \
      BACKEND_EMIT=exe \
      BACKEND_TARGET="$target" \
      BACKEND_INPUT="$fixture" \
      BACKEND_OUTPUT="$exe" \
      "$driver"
  elif [ "$status" -ne 0 ]; then
    exit "$status"
  fi

  if [ ! -x "$exe" ]; then
    echo "[Error] missing exe output: $exe" 1>&2
    exit 1
  fi

  dsym_log="$out_dir/dsymutil.$(basename "$exe").txt"
  dsymutil "$exe" -o "$dsym" >"$dsym_log" 2>&1 || {
    echo "[Error] dsymutil failed: $exe" 1>&2
    tail -n 80 "$dsym_log" 1>&2 || true
    exit 1
  }

  dwarf="$dsym/Contents/Resources/DWARF/$(basename "$exe")"
  if [ ! -s "$dwarf" ]; then
    echo "[Error] missing dSYM DWARF file: $dwarf" 1>&2
    exit 1
  fi

  nm -g "$dwarf" | grep -q "_main" || {
    echo "[Error] missing _main in dSYM: $dwarf" 1>&2
    exit 1
  }
}

build_and_check_dsym "arm64-apple-darwin" "$out_dir/hello_puts.debug.arm64"
build_and_check_dsym "x86_64-apple-darwin" "$out_dir/hello_puts.debug.x86_64"

echo "verify_backend_debug ok"
