#!/usr/bin/env sh
. "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/env_prefix_bridge.sh"
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

driver="${BACKEND_DRIVER:-}"
target="${BACKEND_TARGET:-}"
linker_mode="${BACKEND_LINKER:-}"

while [ "${1:-}" != "" ]; do
  case "$1" in
    --driver:*)
      driver="${1#--driver:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    --linker:*)
      linker_mode="${1#--linker:}"
      ;;
    --help|-h)
      echo "Usage: src/tooling/backend_link_env.sh [--driver:<path>] [--target:<triple>] [--linker:<self|system|auto>]" 1>&2
      exit 0
      ;;
    *)
      echo "[Error] unknown arg: $1" 1>&2
      exit 2
      ;;
  esac
  shift || true
done

if [ "$driver" = "" ]; then
  driver="$(sh src/tooling/backend_driver_path.sh)"
fi

if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  target="$(sh src/tooling/detect_host_target.sh)"
fi

target_supports_self_linker() {
  t="$1"
  case "$t" in
    *darwin*)
      case "$t" in
        *arm64*|*aarch64*|*x86_64*|*amd64*) return 0 ;;
      esac
      return 1
      ;;
    *linux*|*android*)
      case "$t" in
        *arm64*|*aarch64*|*riscv64*) return 0 ;;
      esac
      return 1
      ;;
    *windows*|*msvc*)
      case "$t" in
        *arm64*|*aarch64*) return 0 ;;
      esac
      return 1
      ;;
  esac
  return 1
}

case "$linker_mode" in
  self)
    ;;
  system)
    printf "BACKEND_LINKER=system\n"
    exit 0
    ;;
  ""|auto)
    if target_supports_self_linker "$target"; then
      linker_mode="self"
    else
      printf "BACKEND_LINKER=system\n"
      exit 0
    fi
    ;;
  *)
    echo "[Error] invalid linker mode: $linker_mode" 1>&2
    exit 2
    ;;
esac

mkdir -p chengcache
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="${BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"
if [ ! -f "$runtime_obj" ]; then
  echo "[backend_link_env] missing runtime object: $runtime_obj" >&2
  echo "  hint: prepare runtime object for target=$target before self-link gates" >&2
  exit 1
fi
printf "BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1 BACKEND_RUNTIME_OBJ=%s\n" "$runtime_obj"
