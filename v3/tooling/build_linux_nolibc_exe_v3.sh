#!/usr/bin/env sh
set -eu

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
compiler="${CHENG_V3_LINUX_EXE_COMPILER:-$root/artifacts/v3_backend_driver/cheng}"
source_path="${CHENG_V3_LINUX_EXE_SOURCE:-}"
target="${CHENG_V3_LINUX_EXE_TARGET:-aarch64-unknown-linux-gnu}"
out_bin="${CHENG_V3_LINUX_EXE_OUT:-}"
report_path="${CHENG_V3_LINUX_EXE_REPORT:-}"

if [ -z "$source_path" ] || [ -z "$out_bin" ]; then
  echo "v3 linux exe: missing source/out env" >&2
  exit 1
fi

if [ ! -x "$compiler" ]; then
  sh "$root/v3/tooling/build_backend_driver_v3.sh"
fi

mkdir -p "$(dirname -- "$out_bin")"

if [ -n "$report_path" ]; then
  exec "$compiler" system-link-exec \
    --root "$root/v3" \
    --in "$source_path" \
    --emit exe \
    --target "$target" \
    --out "$out_bin" \
    --report-out "$report_path"
fi

exec "$compiler" system-link-exec \
  --root "$root/v3" \
  --in "$source_path" \
  --emit exe \
  --target "$target" \
  --out "$out_bin"
