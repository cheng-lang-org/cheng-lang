#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="${TOOLING_ROOT:-$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)}"
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
  driver="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} backend_driver_path)"
fi

if [ "$target" = "" ] || [ "$target" = "auto" ]; then
  target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target)"
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

case "$target" in
  ""|auto|native|host)
    target="$(${TOOLING_SELF_BIN:-artifacts/tooling_cmd/cheng_tooling} detect_host_target)"
    ;;
  darwin_arm64|darwin_aarch64)
    target="arm64-apple-darwin"
    ;;
  darwin_x86_64|darwin_amd64)
    target="x86_64-apple-darwin"
    ;;
  linux_arm64|linux_aarch64)
    target="aarch64-unknown-linux-gnu"
    ;;
  linux_x86_64|linux_amd64)
    target="x86_64-unknown-linux-gnu"
    ;;
  linux_riscv64)
    target="riscv64-unknown-linux-gnu"
    ;;
  windows_arm64|windows_aarch64)
    target="aarch64-pc-windows-msvc"
    ;;
  windows_x86_64|windows_amd64)
    target="x86_64-pc-windows-msvc"
    ;;
esac

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

runtime_nm_has_sym() {
  sym="$1"
  printf '%s\n' "$nm_out" | awk '{print $NF}' | sed 's/^_//' | grep -Fxq "$sym"
}

runtime_has_core_symbols() {
  obj="$1"
  [ -f "$obj" ] || return 1
  if ! command -v nm >/dev/null 2>&1; then
    return 0
  fi
  nm_out="$(nm -g "$obj" 2>/dev/null || true)"
  [ "$nm_out" != "" ] || return 1
  runtime_nm_has_sym cheng_strlen &&
  runtime_nm_has_sym cheng_memcpy &&
  runtime_nm_has_sym cheng_memset &&
  runtime_nm_has_sym cheng_malloc &&
  runtime_nm_has_sym cheng_free &&
  runtime_nm_has_sym cheng_mem_retain &&
  runtime_nm_has_sym cheng_mem_release &&
  runtime_nm_has_sym cheng_seq_get &&
  runtime_nm_has_sym cheng_seq_set &&
  runtime_nm_has_sym cheng_strcmp &&
  runtime_nm_has_sym cheng_f32_bits_to_i64 &&
  runtime_nm_has_sym cheng_f64_bits_to_i64
}

resolve_runtime_obj() {
  if [ "${BACKEND_RUNTIME_OBJ:-}" != "" ]; then
    printf '%s\n' "${BACKEND_RUNTIME_OBJ}"
    return 0
  fi
  candidates="
chengcache/runtime_selflink/system_helpers.backend.fullcompat.${target}.o
chengcache/runtime_selflink/system_helpers.backend.combined.${target}.o
artifacts/backend_mm/system_helpers.backend.combined.${target}.o
chengcache/system_helpers.backend.cheng.${target}.o
artifacts/backend_selfhost_self_obj/stage1.native.runtime.dedup.o
artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.stage1shim.o
chengcache/system_helpers.backend.cheng.o
artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.o
"
  for cand in $candidates; do
    [ "$cand" != "" ] || continue
    if [ -f "$cand" ] && runtime_has_core_symbols "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  for cand in $candidates; do
    [ "$cand" != "" ] || continue
    if [ -f "$cand" ]; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  return 1
}

runtime_obj="$(resolve_runtime_obj || true)"
if [ "$runtime_obj" = "" ] || [ ! -f "$runtime_obj" ]; then
  echo "[backend_link_env] missing runtime object for target=$target" >&2
  echo "  hint: set BACKEND_RUNTIME_OBJ or prepare chengcache/runtime_selflink/system_helpers.backend.fullcompat.${target}.o" >&2
  exit 1
fi
if ! runtime_has_core_symbols "$runtime_obj"; then
  echo "[backend_link_env] warn: runtime object may be incomplete for self-link: $runtime_obj" >&2
fi
printf "BACKEND_LINKER=self BACKEND_NO_RUNTIME_C=1 BACKEND_RUNTIME_OBJ=%s\n" "$runtime_obj"
