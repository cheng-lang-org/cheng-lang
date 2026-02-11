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


out_dir="artifacts/backend_mvp"

mkdir -p "$out_dir"

# Pure Cheng runtime smoke (no `system_helpers.c` in the link step).
cheng_rt_obj="chengcache/system_helpers.backend.cheng.o"
if [ ! -f "$cheng_rt_obj" ] || [ "src/std/system_helpers_backend.cheng" -nt "$cheng_rt_obj" ]; then
  env \
    CHENG_BACKEND_MULTI=0 \
    CHENG_BACKEND_MULTI_FORCE=0 \
    CHENG_BACKEND_ALLOW_NO_MAIN=1 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_EMIT=obj \
    CHENG_BACKEND_TARGET="$target" \
    CHENG_BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
    CHENG_BACKEND_OUTPUT="$cheng_rt_obj" \
    "$driver"
fi
rt_out_dir="artifacts/backend_mvp_cheng_runtime"
mkdir -p "$rt_out_dir"
for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/mm_live_balance.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$rt_out_dir/${base}"
  if [ "$linker_mode" = "self" ]; then
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_MULTI=0 \
    CHENG_BACKEND_MULTI_FORCE=0 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$cheng_rt_obj" \
    CHENG_BACKEND_TARGET="$target" \
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
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver"
  fi
  "$exe_path"
done

echo "verify_backend_mvp ok"

for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/hello_puts.cheng \
               tests/cheng/backend/fixtures/hello_importc_puts.cheng \
               tests/cheng/backend/fixtures/void_fn_return.cheng \
               tests/cheng/backend/fixtures/return_call9.cheng \
               tests/cheng/backend/fixtures/return_while_sum.cheng \
               tests/cheng/backend/fixtures/return_for_sum.cheng \
               tests/cheng/backend/fixtures/return_deref.cheng \
               tests/cheng/backend/fixtures/return_store_deref.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/${base}"
  if [ "$linker_mode" = "self" ]; then
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_MULTI=0 \
    CHENG_BACKEND_MULTI_FORCE=0 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$cheng_rt_obj" \
    CHENG_BACKEND_TARGET="$target" \
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
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver"
  fi
  "$exe_path"
done
