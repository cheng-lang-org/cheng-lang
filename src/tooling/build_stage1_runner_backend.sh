#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/build_stage1_runner_backend.sh [--out:<path>] [--target:<triple>]
                                            [--driver:<path>]

Builds stage1_runner using the self-hosted backend driver (stage2).

Notes:
  - Requires CHENG_BACKEND_DRIVER to point to a stage2 backend driver (or pass --driver:...)
  - Default target is inferred from the host.
USAGE
}

out="artifacts/fullchain/stage1_runner.backend"
target=""
driver_override=""

while [ "${1:-}" != "" ]; do
  case "$1" in
    --out:*)
      out="${1#--out:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --driver:*)
      driver_override="${1#--driver:}"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

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

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"

if [ "$target" = "" ]; then
  case "$host_os/$host_arch" in
    Darwin/arm64)
      target="arm64-apple-darwin"
      ;;
    Linux/aarch64|Linux/arm64)
      target="aarch64-unknown-linux-gnu"
      ;;
    *)
      echo "build_stage1_runner_backend skip: unsupported host=$host_os/$host_arch" 1>&2
      exit 2
      ;;
  esac
fi

if [ "$driver_override" != "" ]; then
  export CHENG_BACKEND_DRIVER="$driver_override"
fi

stage2_driver="${CHENG_BACKEND_DRIVER:-}"
if [ "$stage2_driver" = "" ]; then
  echo "[Error] build_stage1_runner_backend requires CHENG_BACKEND_DRIVER (expected stage2, e.g. artifacts/backend_selfhost_self_obj/backend_mvp_driver.stage2)" 1>&2
  exit 1
fi

case "$stage2_driver" in
  *stage2*)
    ;;
  *)
    echo "[Error] build_stage1_runner_backend expects a stage2 driver (path should include .stage2): $stage2_driver" 1>&2
    exit 1
    ;;
esac

driver="$(sh src/tooling/backend_driver_path.sh)"

out_dir="$(dirname "$out")"
mkdir -p "$out_dir"

CHENG_MM=off \
CHENG_BACKEND_VALIDATE=1 \
CHENG_BACKEND_EMIT=exe \
CHENG_BACKEND_TARGET="$target" \
CHENG_BACKEND_FRONTEND=stage1 \
CHENG_BACKEND_INPUT="src/stage1/frontend_stage1.cheng" \
CHENG_BACKEND_OUTPUT="$out" \
"$driver" >/dev/null

if [ ! -x "$out" ]; then
  echo "[Error] build_stage1_runner_backend missing output: $out" 1>&2
  exit 1
fi

echo "build_stage1_runner_backend ok: $out (target=$target)"
