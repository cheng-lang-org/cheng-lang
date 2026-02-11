#!/usr/bin/env sh
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$root"

driver="${CHENG_BACKEND_DRIVER:-}"
target="${CHENG_BACKEND_TARGET:-}"
linker_mode="${CHENG_BACKEND_LINKER:-}"

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

case "$linker_mode" in
  self)
    ;;
  system)
    printf "CHENG_BACKEND_LINKER=system\n"
    exit 0
    ;;
  ""|auto)
    printf "\n"
    exit 0
    ;;
  *)
    echo "[Error] invalid linker mode: $linker_mode" 1>&2
    exit 2
    ;;
esac

mkdir -p chengcache
safe_target="$(printf '%s' "$target" | tr -c 'A-Za-z0-9._-' '_' | tr -s '_')"
runtime_obj="${CHENG_BACKEND_RUNTIME_OBJ:-chengcache/system_helpers.backend.cheng.${safe_target}.o}"

if [ ! -f "$runtime_obj" ] || [ "src/std/system_helpers_backend.cheng" -nt "$runtime_obj" ]; then
  env \
    CHENG_MM="${CHENG_MM:-orc}" \
    CHENG_BACKEND_ALLOW_NO_MAIN=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_FRONTEND=mvp \
    CHENG_BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
    CHENG_BACKEND_OUTPUT="$runtime_obj" \
    "$driver" >/dev/null
fi

printf "CHENG_BACKEND_LINKER=self CHENG_BACKEND_NO_RUNTIME_C=1 CHENG_BACKEND_RUNTIME_OBJ=%s\n" "$runtime_obj"
