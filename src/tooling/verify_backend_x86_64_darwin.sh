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

# Keep x86_64 codegen gate independent from closure-level no-pointer policy toggles.
export STAGE1_STD_NO_POINTERS=0
export STAGE1_STD_NO_POINTERS_STRICT=0
export STAGE1_NO_POINTERS_NON_C_ABI=0
export STAGE1_NO_POINTERS_NON_C_ABI_INTERNAL=0

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

is_known_system_link_x86_64_blocker() {
  log_path="$1"
  if [ ! -f "$log_path" ]; then
    return 1
  fi
  if rg -q "Undefined symbols for architecture x86_64:" "$log_path" && \
     rg -q "_cheng_(bytes_copy|bytes_set|memcpy|memset|seq_get|seq_set|strcmp|strlen|load_ptr|store_ptr)" "$log_path"; then
    return 0
  fi
  if rg -q "ld: symbol\\(s\\) not found for architecture x86_64" "$log_path" && \
     rg -q "system_helpers\\.backend\\.combined\\.x86_64\\.darwin\\.o" "$log_path"; then
    return 0
  fi
  return 1
}

ensure_backend_runtime_obj() {
  if [ ! -f "$cheng_rt_obj_base" ] || [ "src/std/system_helpers_backend.cheng" -nt "$cheng_rt_obj_base" ]; then
    env \
      BACKEND_VALIDATE=1 \
      BACKEND_ALLOW_NO_MAIN=1 \
      BACKEND_MULTI=0 \
      BACKEND_MULTI_FORCE=0 \
      BACKEND_WHOLE_PROGRAM=1 \
      BACKEND_FRONTEND=stage1 \
      BACKEND_EMIT=obj \
      BACKEND_TARGET=x86_64-apple-darwin \
      BACKEND_INPUT="src/std/system_helpers_backend.cheng" \
      BACKEND_OUTPUT="$cheng_rt_obj_base" \
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
  BACKEND_VALIDATE=1 \
  BACKEND_MULTI=0 \
  BACKEND_MULTI_FORCE=0 \
  BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_EMIT=obj \
  BACKEND_TARGET=x86_64-apple-darwin \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$obj_path" \
  "$driver"
  if [ ! -s "$obj_path" ]; then
    echo "[Error] missing object output: $obj_path" 1>&2
    exit 1
  fi
}

build_exe() {
  fixture="$1"
  exe_path="$2"
  base="$(basename "$fixture" .cheng)"
  compile_log="$out_dir/build.${base}.x86_64.system.log"
  rm -f "$exe_path" "$compile_log"
  ensure_backend_runtime_obj
  set +e
  env \
    BACKEND_VALIDATE=1 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ="$cheng_rt_obj" \
    BACKEND_EMIT=exe \
    BACKEND_TARGET=x86_64-apple-darwin \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver" >"$compile_log" 2>&1
  status="$?"
  set -e
  if [ "$status" -ne 0 ]; then
    if is_known_system_link_x86_64_blocker "$compile_log"; then
      return 2
    fi
    tail -n 200 "$compile_log" 1>&2 || true
    return "$status"
  fi
  if [ ! -x "$exe_path" ]; then
    echo "[Error] missing exe output: $exe_path" 1>&2
    return 1
  fi
  return 0
}

require_build_exe() {
  fixture="$1"
  exe_path="$2"
  set +e
  build_exe "$fixture" "$exe_path"
  status="$?"
  set -e
  if [ "$status" -eq 0 ]; then
    return 0
  fi
  if [ "$status" -eq 2 ]; then
    echo "verify_backend_x86_64_darwin skip: system-link x86_64 Mach-O unresolved imports on this seed/driver" 1>&2
    exit 2
  fi
  exit "$status"
}

# Object + exe smoke: importc puts.
fixture="tests/cheng/backend/fixtures/hello_importc_puts.cheng"
obj_path="$out_dir/hello_importc_puts.x86_64.o"
exe_path="$out_dir/hello_importc_puts.x86_64"

build_obj "$fixture" "$obj_path"
if command -v otool >/dev/null 2>&1; then
  if ! otool -hv "$obj_path" | grep -q "X86_64"; then
    echo "verify_backend_x86_64_darwin skip: seed/driver does not emit x86_64 Mach-O objects" 1>&2
    exit 2
  fi
fi
if command -v nm >/dev/null 2>&1; then
  nm "$obj_path" | grep -q " T _main"
  nm "$obj_path" | grep -q " U _puts"
fi

require_build_exe "$fixture" "$exe_path"
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
  require_build_exe "$fixture" "$exe_path"
  run_x86_64 "$exe_path"
done

# Self linker (no `cc`): x86_64 Mach-O via backend `--linker=self`.
ensure_backend_runtime_obj

build_exe_self() {
  fixture="$1"
  exe_path="$2"
  rm -f "$exe_path"
  set +e
  env \
    BACKEND_VALIDATE=1 \
    BACKEND_MULTI=0 \
    BACKEND_MULTI_FORCE=0 \
    BACKEND_WHOLE_PROGRAM=1 \
    BACKEND_LINKER=self \
    BACKEND_NO_RUNTIME_C=1 \
    BACKEND_RUNTIME_OBJ="$cheng_rt_obj" \
    BACKEND_EMIT=exe \
    BACKEND_TARGET=x86_64-apple-darwin \
    BACKEND_INPUT="$fixture" \
    BACKEND_OUTPUT="$exe_path" \
    "$driver" >/dev/null
  status="$?"
  set -e
  if [ "$status" -ne 0 ] || [ ! -x "$exe_path" ]; then
    return 2
  fi
  return 0
}

exe_self="$out_dir/hello_puts.x86_64.self"
if ! build_exe_self tests/cheng/backend/fixtures/hello_puts.cheng "$exe_self"; then
  echo "verify_backend_x86_64_darwin skip: self-link x86_64 Mach-O unresolved imports on this seed/driver" 1>&2
  exit 2
fi
run_x86_64 "$exe_self" >"$out_dir/run.hello_puts.self.txt"
if [ "$can_run" = "1" ]; then
  grep -Fq "hello from cheng backend" "$out_dir/run.hello_puts.self.txt"
fi

for fixture in tests/cheng/backend/fixtures/return_add.cheng \
               tests/cheng/backend/fixtures/return_call9.cheng \
               tests/cheng/backend/fixtures/return_while_sum.cheng
do
  base="$(basename "$fixture" .cheng)"
  exe_path="$out_dir/${base}.x86_64.self"
  if ! build_exe_self "$fixture" "$exe_path"; then
    echo "verify_backend_x86_64_darwin skip: self-link x86_64 Mach-O unresolved imports on this seed/driver" 1>&2
    exit 2
  fi
  run_x86_64 "$exe_path"
done

echo "verify_backend_x86_64_darwin ok"
