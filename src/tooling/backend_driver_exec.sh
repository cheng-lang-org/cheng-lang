#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

v3_driver="$root/artifacts/v3_backend_driver/cheng"

usage() {
  cat <<'EOF'
Usage:
  src/tooling/backend_driver_exec.sh <input.cheng> --output:<path> [legacy backend_driver flags]

Notes:
  - Old backend driver has been removed.
  - This wrapper translates the legacy backend_driver CLI/env surface to
    `artifacts/v3_backend_driver/cheng system-link-exec`.
  - Only compile request fields are kept: input/output/emit/target/report.
EOF
}

read_env_or_empty() {
  key="$1"
  eval "printf '%s\n' \"\${$key:-}\""
}

read_env_pair_or_empty() {
  left="$(read_env_or_empty "$1")"
  if [ "$left" != "" ]; then
    printf '%s\n' "$left"
    return 0
  fi
  printf '%s\n' "$(read_env_or_empty "$2")"
}

detect_host_target() {
  os_name="$(uname -s 2>/dev/null || echo unknown)"
  arch_name="$(uname -m 2>/dev/null || echo unknown)"
  case "$os_name:$arch_name" in
    Darwin:arm64)
      printf '%s\n' "arm64-apple-darwin"
      ;;
    Darwin:x86_64)
      printf '%s\n' "x86_64-apple-darwin"
      ;;
    Linux:aarch64|Linux:arm64)
      printf '%s\n' "aarch64-unknown-linux-gnu"
      ;;
    Linux:x86_64)
      printf '%s\n' "x86_64-unknown-linux-gnu"
      ;;
    *)
      printf '%s\n' "arm64-apple-darwin"
      ;;
  esac
}

normalize_target() {
  raw="$1"
  case "$raw" in
    ""|auto)
      detect_host_target
      ;;
    *)
      printf '%s\n' "$raw"
      ;;
  esac
}

ensure_v3_driver() {
  if [ -x "$v3_driver" ]; then
    return 0
  fi
  sh "$root/v3/tooling/build_backend_driver_v3.sh" >/dev/null
  if [ ! -x "$v3_driver" ]; then
    echo "[backend_driver_exec] missing v3 backend driver: $v3_driver" >&2
    exit 2
  fi
}

src=""
out=""
emit=""
target=""
report=""

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
  usage
  exit 0
fi

for arg in "$@"; do
  case "$arg" in
    --help|-h)
      usage
      exit 0
      ;;
    --output:*)
      out="${arg#--output:}"
      ;;
    --output=*)
      out="${arg#--output=}"
      ;;
    --out:*)
      out="${arg#--out:}"
      ;;
    --out=*)
      out="${arg#--out=}"
      ;;
    --emit:*)
      emit="${arg#--emit:}"
      ;;
    --emit=*)
      emit="${arg#--emit=}"
      ;;
    --target:*)
      target="${arg#--target:}"
      ;;
    --target=*)
      target="${arg#--target=}"
      ;;
    --report-out:*)
      report="${arg#--report-out:}"
      ;;
    --report-out=*)
      report="${arg#--report-out=}"
      ;;
    --report:*)
      report="${arg#--report:}"
      ;;
    --report=*)
      report="${arg#--report=}"
      ;;
    --frontend:*|--frontend=*|\
    --linker:*|--linker=*|\
    --build-track:*|--build-track=*|\
    --mm:*|--mm=*|\
    --generic-mode:*|--generic-mode=*|\
    --generic-spec-budget:*|--generic-spec-budget=*|\
    --generic-lowering:*|--generic-lowering=*|\
    --jobs:*|--jobs=*|\
    --fn-jobs:*|--fn-jobs=*|\
    --opt-level:*|--opt-level=*|\
    --runtime-obj:*|--runtime-obj=*|\
    --sidecar-mode:*|--sidecar-mode=*|\
    --sidecar-bundle:*|--sidecar-bundle=*|\
    --sidecar-compiler:*|--sidecar-compiler=*|\
    --sidecar-child-mode:*|--sidecar-child-mode=*|\
    --sidecar-outer-compiler:*|--sidecar-outer-compiler=*|\
    --module-cache:*|--module-cache=*|\
    --multi-module-cache:*|--multi-module-cache=*|\
    --module-cache-unstable-allow:*|--module-cache-unstable-allow=*|\
    --allow-no-main|\
    --whole-program|--no-whole-program|\
    --multi|--no-multi|\
    --multi-force|--no-multi-force|\
    --incremental|--no-incremental|\
    --opt|--no-opt|\
    --opt2|--no-opt2)
      ;;
    --*)
      echo "[backend_driver_exec] unsupported legacy flag after old backend_driver removal: $arg" >&2
      exit 2
      ;;
    *)
      if [ "$src" = "" ]; then
        src="$arg"
      else
        echo "[backend_driver_exec] multiple input paths are not supported: $arg" >&2
        exit 2
      fi
      ;;
  esac
done

if [ "$src" = "" ]; then
  src="$(read_env_pair_or_empty BACKEND_INPUT CHENG_BACKEND_INPUT)"
fi
if [ "$out" = "" ]; then
  out="$(read_env_pair_or_empty BACKEND_OUTPUT CHENG_BACKEND_OUTPUT)"
fi
if [ "$emit" = "" ]; then
  emit="$(read_env_pair_or_empty BACKEND_EMIT CHENG_BACKEND_EMIT)"
fi
if [ "$target" = "" ]; then
  target="$(read_env_pair_or_empty BACKEND_TARGET CHENG_BACKEND_TARGET)"
fi
if [ "$report" = "" ]; then
  report="$(read_env_pair_or_empty BACKEND_REPORT_OUT CHENG_BACKEND_REPORT_OUT)"
fi

if [ "$emit" = "" ]; then
  emit="exe"
fi
target="$(normalize_target "$target")"

if [ "$src" = "" ]; then
  echo "[backend_driver_exec] missing input source" >&2
  exit 2
fi
if [ "$out" = "" ]; then
  echo "[backend_driver_exec] missing output path" >&2
  exit 2
fi

ensure_v3_driver

if [ "$report" != "" ]; then
  exec "$v3_driver" system-link-exec \
    --root:"$root" \
    --in:"$src" \
    --emit:"$emit" \
    --target:"$target" \
    --out:"$out" \
    --report-out:"$report"
fi

exec "$v3_driver" system-link-exec \
  --root:"$root" \
  --in:"$src" \
  --emit:"$emit" \
  --target:"$target" \
  --out:"$out"
