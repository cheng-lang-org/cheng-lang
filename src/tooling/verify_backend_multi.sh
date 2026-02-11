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


out_dir="artifacts/backend_multi"
mkdir -p "$out_dir"

fixture="tests/cheng/backend/fixtures/return_add.cheng"
exe_path="$out_dir/return_add"

if [ "$linker_mode" = "self" ]; then
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_LINKER=self \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_RUNTIME_OBJ="$runtime_obj" \
  CHENG_BACKEND_MULTI=1 \
  CHENG_BACKEND_MULTI_FORCE=1 \
  CHENG_BACKEND_JOBS=4 \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
else
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_LINKER=system \
  CHENG_BACKEND_MULTI=1 \
  CHENG_BACKEND_MULTI_FORCE=1 \
  CHENG_BACKEND_JOBS=4 \
  CHENG_BACKEND_TARGET="$target" \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
fi

"$exe_path"

count="$(ls "$exe_path.objs"/*.o | wc -l | tr -d ' ')"
test "$count" -ge 1

echo "verify_backend_multi ok"
