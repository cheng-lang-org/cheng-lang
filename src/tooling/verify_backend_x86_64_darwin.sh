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

host_os="$(uname -s 2>/dev/null || echo unknown)"
if [ "$host_os" != "Darwin" ]; then
  echo "verify_backend_x86_64_darwin skip: darwin-only (host_os=$host_os)" 1>&2
  exit 2
fi
if ! command -v cc >/dev/null 2>&1; then
  echo "verify_backend_x86_64_darwin skip: missing cc" 1>&2
  exit 2
fi

driver="$(sh src/tooling/backend_driver_path.sh)"

out_dir="artifacts/backend_x86_64_darwin"
mkdir -p "$out_dir"

cheng_rt_obj_base="chengcache/system_helpers.backend.cheng.x86_64.darwin.o"
float_bits_src="src/runtime/native/system_helpers_float_bits.c"
float_bits_obj="chengcache/system_helpers_float_bits.x86_64.darwin.min11.o"
cheng_rt_obj="chengcache/system_helpers.backend.combined.x86_64.darwin.o"

can_run="0"
if command -v arch >/dev/null 2>&1; then
  if arch -x86_64 /usr/bin/true >/dev/null 2>&1; then
    can_run="1"
  fi
fi

run_x86_64() {
  exe="$1"
  shift || true
  if [ "$can_run" != "1" ]; then
    echo "[Warn] verify_backend_x86_64_darwin: missing Rosetta; skip run: $exe" 1>&2
    return 0
  fi
  arch -x86_64 "$exe" "$@"
}

ensure_backend_runtime_obj() {
  if [ ! -f "$cheng_rt_obj_base" ] || [ "src/std/system_helpers_backend.cheng" -nt "$cheng_rt_obj_base" ]; then
    env \
      CHENG_BACKEND_VALIDATE=1 \
      CHENG_BACKEND_ALLOW_NO_MAIN=1 \
      CHENG_BACKEND_MULTI=0 \
      CHENG_BACKEND_MULTI_FORCE=0 \
      CHENG_BACKEND_WHOLE_PROGRAM=1 \
      CHENG_BACKEND_FRONTEND=mvp \
      CHENG_BACKEND_EMIT=obj \
      CHENG_BACKEND_TARGET=x86_64-apple-darwin \
      CHENG_BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
      CHENG_BACKEND_OUTPUT="$cheng_rt_obj_base" \
      "$driver" >/dev/null
  fi

  if [ ! -f "$float_bits_obj" ] || [ "$float_bits_src" -nt "$float_bits_obj" ]; then
    env MACOSX_DEPLOYMENT_TARGET=11.0 \
      cc -target x86_64-apple-darwin -mmacosx-version-min=11.0 -O2 -c "$float_bits_src" -o "$float_bits_obj"
  fi

  if [ ! -f "$cheng_rt_obj" ] || [ "$cheng_rt_obj_base" -nt "$cheng_rt_obj" ] || [ "$float_bits_obj" -nt "$cheng_rt_obj" ]; then
    ld -r -arch x86_64 "$cheng_rt_obj_base" "$float_bits_obj" -o "$cheng_rt_obj"
  fi
}

build_obj() {
  fixture="$1"
  obj_path="$2"
  rm -f "$obj_path"
  CHENG_BACKEND_VALIDATE=1 \
  CHENG_BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_EMIT=obj \
  CHENG_BACKEND_TARGET=x86_64-apple-darwin \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$obj_path" \
  "$driver"
  if [ ! -s "$obj_path" ]; then
    echo "[Error] missing object output: $obj_path" 1>&2
    exit 1
  fi
}

build_exe() {
  fixture="$1"
  exe_path="$2"
  rm -f "$exe_path"
  ensure_backend_runtime_obj
  CHENG_BACKEND_VALIDATE=1 \
  CHENG_BACKEND_MULTI=0 \
  CHENG_BACKEND_MULTI_FORCE=0 \
  CHENG_BACKEND_WHOLE_PROGRAM=1 \
  CHENG_BACKEND_NO_RUNTIME_C=1 \
  CHENG_BACKEND_RUNTIME_OBJ="$cheng_rt_obj" \
  CHENG_BACKEND_EMIT=exe \
  CHENG_BACKEND_TARGET=x86_64-apple-darwin \
  CHENG_BACKEND_INPUT="$fixture" \
  CHENG_BACKEND_OUTPUT="$exe_path" \
  "$driver"
  if [ ! -x "$exe_path" ]; then
    echo "[Error] missing exe output: $exe_path" 1>&2
    exit 1
  fi
}

# Object + exe smoke: importc puts.
fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_path="$out_dir/hello_importc_puts.x86_64.o"
exe_path="$out_dir/hello_importc_puts.x86_64"

build_obj "$fixture" "$obj_path"
if command -v nm >/dev/null 2>&1; then
  nm "$obj_path" | grep -q " T _main"
  nm "$obj_path" | grep -q " U _puts"
fi
if command -v otool >/dev/null 2>&1; then
  otool -hv "$obj_path" | grep -q "X86_64"
fi

build_exe "$fixture" "$exe_path"
if [ "$can_run" = "1" ]; then
  run_x86_64 "$exe_path" >"$out_dir/run.hello_importc_puts.txt"
  grep -Fq "hello importc puts" "$out_dir/run.hello_importc_puts.txt"
else
  echo "[Warn] verify_backend_x86_64_darwin: missing Rosetta; skip output check" 1>&2
fi

# Deeper x86_64 exe coverage (SysV ABI + control flow + concurrency).
for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/return_call9.cheng \
               tests/cheng/backend/fixtures/return_while_sum.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/${base}.x86_64"
  build_exe "$fixture" "$exe_path"
  run_x86_64 "$exe_path"
done

# Self linker (no `cc`): x86_64 Mach-O via backend `--linker=self`.
ensure_backend_runtime_obj

build_exe_self() {
  fixture="$1"
  exe_path="$2"
  rm -f "$exe_path"
  env \
    CHENG_BACKEND_VALIDATE=1 \
    CHENG_BACKEND_MULTI=0 \
    CHENG_BACKEND_MULTI_FORCE=0 \
    CHENG_BACKEND_WHOLE_PROGRAM=1 \
    CHENG_BACKEND_LINKER=self \
    CHENG_BACKEND_NO_RUNTIME_C=1 \
    CHENG_BACKEND_RUNTIME_OBJ="$cheng_rt_obj" \
    CHENG_BACKEND_EMIT=exe \
    CHENG_BACKEND_TARGET=x86_64-apple-darwin \
    CHENG_BACKEND_INPUT="$fixture" \
    CHENG_BACKEND_OUTPUT="$exe_path" \
    "$driver" >/dev/null
  if [ ! -x "$exe_path" ]; then
    echo "[Error] missing exe output (self-link): $exe_path" 1>&2
    exit 1
  fi
}

exe_self="$out_dir/hello_importc_puts.x86_64.self"
build_exe_self tests/cheng/backend/fixtures/hello_importc_puts.cheng "$exe_self"
run_x86_64 "$exe_self" >"$out_dir/run.hello_importc_puts.self.txt"
if [ "$can_run" = "1" ]; then
  grep -Fq "hello importc puts" "$out_dir/run.hello_importc_puts.self.txt"
fi

for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/return_call9.cheng \
               tests/cheng/backend/fixtures/return_while_sum.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/${base}.x86_64.self"
  build_exe_self "$fixture" "$exe_path"
  run_x86_64 "$exe_path"
done

echo "verify_backend_x86_64_darwin ok"
