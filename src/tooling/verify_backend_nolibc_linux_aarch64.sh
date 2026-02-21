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
target="${BACKEND_NOLIBC_TARGET:-aarch64-unknown-linux-gnu}"
out_dir="artifacts/backend_nolibc_linux_aarch64"
mkdir -p "$out_dir"

runtime_src="src/std/system_helpers_backend_nolibc_linux_aarch64.cheng"
runtime_obj="$out_dir/system_helpers_backend_nolibc_linux_aarch64.$target.o"

if [ ! -f "$runtime_src" ]; then
  echo "[verify_backend_nolibc_linux_aarch64] missing runtime source: $runtime_src" >&2
  exit 1
fi

if [ ! -f "$runtime_obj" ] || [ "$runtime_src" -nt "$runtime_obj" ]; then
  BACKEND_ALLOW_NO_MAIN=1 \
  BACKEND_WHOLE_PROGRAM=1 \
  BACKEND_EMIT=obj \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_INPUT="$runtime_src" \
  BACKEND_OUTPUT="$runtime_obj" \
    "$driver" >/dev/null
fi

if [ ! -s "$runtime_obj" ]; then
  echo "[verify_backend_nolibc_linux_aarch64] missing runtime object: $runtime_obj" >&2
  exit 1
fi

find_objdump() {
  if command -v xcrun >/dev/null 2>&1; then
    p="$(xcrun --find llvm-objdump 2>/dev/null || true)"
    if [ "$p" != "" ]; then
      printf "%s\n" "$p"
      return 0
    fi
  fi
  for n in llvm-objdump llvm-objdump-19 llvm-objdump-18 llvm-objdump-17 llvm-objdump-16 llvm-objdump-15 llvm-objdump-14; do
    if command -v "$n" >/dev/null 2>&1; then
      command -v "$n"
      return 0
    fi
  done
  return 1
}

find_nm() {
  if command -v xcrun >/dev/null 2>&1; then
    p="$(xcrun --find llvm-nm 2>/dev/null || true)"
    if [ "$p" != "" ]; then
      printf "%s\n" "$p"
      return 0
    fi
  fi
  for n in llvm-nm llvm-nm-19 llvm-nm-18 llvm-nm-17 llvm-nm-16 llvm-nm-15 llvm-nm-14 nm; do
    if command -v "$n" >/dev/null 2>&1; then
      command -v "$n"
      return 0
    fi
  done
  return 1
}

objdump="$(find_objdump || true)"
nm_tool="$(find_nm || true)"
if [ "$objdump" = "" ] || [ "$nm_tool" = "" ]; then
  echo "[verify_backend_nolibc_linux_aarch64] missing llvm-objdump/llvm-nm" >&2
  exit 2
fi

build_exe() {
  fixture="$1"
  exe_path="$2"
  BACKEND_ELF_PROFILE=nolibc \
  BACKEND_LINKER=self \
  BACKEND_NO_RUNTIME_C=1 \
  BACKEND_RUNTIME_OBJ="$runtime_obj" \
  BACKEND_EMIT=exe \
  BACKEND_TARGET="$target" \
  BACKEND_FRONTEND=stage1 \
  BACKEND_INPUT="$fixture" \
  BACKEND_OUTPUT="$exe_path" \
    "$driver" >/dev/null
  if [ ! -s "$exe_path" ]; then
    echo "[verify_backend_nolibc_linux_aarch64] missing output: $exe_path" >&2
    exit 1
  fi
}

validate_static() {
  exe_path="$1"
  base="$2"

  "$objdump" --private-headers "$exe_path" > "$out_dir/$base.objdump.private.txt"

  grep -q "file format elf64-littleaarch64" "$out_dir/$base.objdump.private.txt"
  if grep -q "INTERP" "$out_dir/$base.objdump.private.txt"; then
    echo "[verify_backend_nolibc_linux_aarch64] unexpected PT_INTERP: $exe_path" >&2
    exit 1
  fi
  if grep -q "DYNAMIC" "$out_dir/$base.objdump.private.txt"; then
    echo "[verify_backend_nolibc_linux_aarch64] unexpected PT_DYNAMIC: $exe_path" >&2
    exit 1
  fi

  undef_out="$($nm_tool -u "$exe_path" 2>/dev/null || true)"
  if [ "$(printf '%s' "$undef_out" | tr -d '[:space:]')" != "" ]; then
    echo "[verify_backend_nolibc_linux_aarch64] unexpected undefined symbols in: $exe_path" >&2
    printf "%s\n" "$undef_out" >&2
    exit 1
  fi

  if strings "$exe_path" | grep -Eq "libc\.so|ld-linux-aarch64"; then
    echo "[verify_backend_nolibc_linux_aarch64] dynamic libc loader strings found: $exe_path" >&2
    exit 1
  fi
}

hello_fixture="tests/cheng/backend/fixtures/hello_puts.cheng"
add_fixture="tests/cheng/backend/fixtures/return_add.cheng"
mm_fixture="tests/cheng/backend/fixtures/mm_live_balance.cheng"

hello_exe="$out_dir/hello_puts.$target.nolibc"
add_exe="$out_dir/return_add.$target.nolibc"
mm_exe="$out_dir/mm_live_balance.$target.nolibc"

build_exe "$hello_fixture" "$hello_exe"
build_exe "$add_fixture" "$add_exe"
build_exe "$mm_fixture" "$mm_exe"

validate_static "$hello_exe" "hello_puts.$target.nolibc"
validate_static "$add_exe" "return_add.$target.nolibc"
validate_static "$mm_exe" "mm_live_balance.$target.nolibc"

host_os="$(uname -s 2>/dev/null || echo unknown)"
host_arch="$(uname -m 2>/dev/null || echo unknown)"
case "$host_os/$host_arch" in
  Linux/aarch64|Linux/arm64)
    "$hello_exe" > "$out_dir/run.hello_puts.txt"
    grep -Fq "hello from cheng backend" "$out_dir/run.hello_puts.txt"

    "$add_exe"
    "$mm_exe"
    ;;
  *)
    echo "[verify_backend_nolibc_linux_aarch64] run phase skipped: linux/aarch64 only (host=$host_os/$host_arch)" >&2
    ;;
esac

echo "verify_backend_nolibc_linux_aarch64 ok"
