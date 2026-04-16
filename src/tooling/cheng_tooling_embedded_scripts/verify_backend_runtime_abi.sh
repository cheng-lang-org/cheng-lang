#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
cd "$root"

compat_header_file="src/runtime/native/system_helpers.h"
backend_runtime_file="src/std/system_helpers_backend.cheng"
cc_bin="${CC:-cc}"
tmp_dir="$(mktemp -d "${TMPDIR:-/tmp}/cheng_runtime_abi.XXXXXX")"

cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

fail() {
  echo "[verify_backend_runtime_abi] $1" 1>&2
  exit 1
}

require_file() {
  file="$1"
  [ -f "$file" ] || fail "missing file: $file"
}

detect_host_target_shell() {
  host_os="$(uname -s 2>/dev/null || echo unknown)"
  host_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$host_os:$host_arch" in
    Darwin:arm64) echo arm64-apple-darwin ;;
    Darwin:aarch64) echo arm64-apple-darwin ;;
    Darwin:x86_64) echo x86_64-apple-darwin ;;
    Linux:aarch64) echo aarch64-unknown-linux-gnu ;;
    Linux:arm64) echo aarch64-unknown-linux-gnu ;;
    Linux:x86_64) echo x86_64-unknown-linux-gnu ;;
    Linux:riscv64) echo riscv64-unknown-linux-gnu ;;
    *) echo arm64-apple-darwin ;;
  esac
}

target="${BACKEND_TARGET:-$(detect_host_target_shell)}"

required_symbols() {
  cat <<'EOF'
cheng_exec_cmd_ex
cheng_exec_file_pipe_spawn
cheng_pty_is_supported
cheng_pty_spawn
cheng_pipe_spawn
cheng_pty_read
cheng_fd_read
cheng_fd_read_wait
cheng_pty_write
cheng_pty_close
cheng_pty_wait
cheng_process_signal
cheng_abi_sum_ptr_len_i32
cheng_abi_sum_seq_i32
cheng_abi_borrow_mut_pair_i32
cheng_ffi_handle_register_ptr
cheng_ffi_handle_resolve_ptr
cheng_ffi_handle_invalidate
cheng_ffi_handle_new_i32
cheng_ffi_handle_get_i32
cheng_ffi_handle_add_i32
cheng_ffi_handle_release_i32
cheng_ffi_raw_new_i32
cheng_ffi_raw_get_i32
cheng_ffi_raw_add_i32
cheng_ffi_raw_release_i32
driver_c_write_text_file_bridge
EOF
}

defined_symbols_file() {
  obj="$1"
  out="$2"
  nm -g "$obj" 2>/dev/null \
    | awk '{
        raw=$0;
        sym=$NF;
        gsub(/^_+/, "", sym);
        undef=(raw=="U" || raw ~ /^[[:space:]]*U[[:space:]]/ || raw ~ /[[:space:]]U[[:space:]]/);
        if (!undef && sym != "") print sym;
      }' \
    | sort -u >"$out"
}

runtime_has_required_symbols() {
  obj="$1"
  sym_file="$tmp_dir/runtime_contract.syms"
  [ -f "$obj" ] || return 1
  defined_symbols_file "$obj" "$sym_file"
  while IFS= read -r sym; do
    [ "$sym" != "" ] || continue
    if ! grep -Fxq "$sym" "$sym_file"; then
      return 1
    fi
  done <<EOF
$(required_symbols)
EOF
  return 0
}

runtime_cc_extra_flags() {
  case "$target" in
    *apple-darwin) printf '%s\n' "-mmacosx-version-min=11.0" ;;
    *) printf '%s\n' "" ;;
  esac
}

runtime_darwin_arch() {
  case "$1" in
    arm64-apple-darwin) printf '%s\n' "arm64" ;;
    x86_64-apple-darwin) printf '%s\n' "x86_64" ;;
    *) return 1 ;;
  esac
}

build_runtime_obj() {
  out="chengcache/runtime_selflink/system_helpers.backend.combined.${target}.o"
  build_dir="$tmp_dir/build"
  extra_flags="$(runtime_cc_extra_flags)"
  mkdir -p "$build_dir" "$(dirname "$out")"
  idx=0
  objs=""
  for src in \
    src/runtime/native/system_helpers_selflink_shim.c \
    src/runtime/native/system_helpers_io_time_bridge.c \
    src/runtime/native/system_helpers_host_process_ffi_bridge.c; do
    require_file "$src"
    obj="$build_dir/obj.$idx.o"
    log="$build_dir/obj.$idx.log"
    if [ "$extra_flags" != "" ]; then
      # shellcheck disable=SC2086
      "$cc_bin" -std=c11 -O2 $extra_flags -c "$src" -o "$obj" >"$log" 2>&1 || {
        sed -n '1,200p' "$log" >&2 || true
        fail "failed to compile runtime bridge: $src"
      }
    else
      "$cc_bin" -std=c11 -O2 -c "$src" -o "$obj" >"$log" 2>&1 || {
        sed -n '1,200p' "$log" >&2 || true
        fail "failed to compile runtime bridge: $src"
      }
    fi
    objs="$objs $obj"
    idx=$((idx + 1))
  done
  link_log="$build_dir/link.log"
  case "$target" in
    *apple-darwin)
      arch="$(runtime_darwin_arch "$target" || true)"
      [ "$arch" != "" ] || fail "unsupported darwin target: $target"
      # shellcheck disable=SC2086
      ld -r -arch "$arch" -o "$out" $objs >"$link_log" 2>&1 || {
        sed -n '1,200p' "$link_log" >&2 || true
        fail "failed to link runtime object: $out"
      }
      ;;
    *)
      # shellcheck disable=SC2086
      ld -r -o "$out" $objs >"$link_log" 2>&1 || {
        sed -n '1,200p' "$link_log" >&2 || true
        fail "failed to link runtime object: $out"
      }
      ;;
  esac
  printf '%s\n' "$out"
}

resolve_runtime_obj() {
  if [ "${BACKEND_RUNTIME_OBJ:-}" != "" ]; then
    if runtime_has_required_symbols "${BACKEND_RUNTIME_OBJ}"; then
      printf '%s\n' "${BACKEND_RUNTIME_OBJ}"
      return 0
    fi
    fail "explicit BACKEND_RUNTIME_OBJ missing required runtime symbols: ${BACKEND_RUNTIME_OBJ}"
  fi
  for cand in \
    "chengcache/runtime_selflink/system_helpers.backend.fullcompat.${target}.o" \
    "chengcache/runtime_selflink/system_helpers.backend.combined.${target}.o" \
    "artifacts/backend_mm/system_helpers.backend.combined.${target}.o" \
    "chengcache/system_helpers.backend.cheng.${target}.o" \
    "artifacts/backend_selfhost_self_obj/stage1.native.runtime.dedup.o" \
    "artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.stage1shim.o" \
    "chengcache/system_helpers.backend.cheng.o" \
    "artifacts/backend_selfhost_self_obj/system_helpers.backend.cheng.o"; do
    if runtime_has_required_symbols "$cand"; then
      printf '%s\n' "$cand"
      return 0
    fi
  done
  built="$(build_runtime_obj)"
  if runtime_has_required_symbols "$built"; then
    printf '%s\n' "$built"
    return 0
  fi
  fail "built runtime object missing required symbols: $built"
}

require_file "$backend_runtime_file"
require_file "$compat_header_file"
command -v rg >/dev/null 2>&1 || fail "rg is required"
command -v nm >/dev/null 2>&1 || fail "nm is required"
command -v "$cc_bin" >/dev/null 2>&1 || fail "C compiler not found: $cc_bin"
command -v ld >/dev/null 2>&1 || fail "ld is required"

runtime_obj="$(resolve_runtime_obj)"
[ -f "$runtime_obj" ] || fail "missing runtime object for target=$target: $runtime_obj"

runtime_symbols="$tmp_dir/runtime.syms"
defined_symbols_file "$runtime_obj" "$runtime_symbols"

while IFS= read -r sym; do
  [ "$sym" != "" ] || continue
  if ! rg -q "$sym" "$backend_runtime_file"; then
    fail "missing pure cheng runtime symbol: $sym"
  fi
  if ! rg -q "$sym" "$compat_header_file"; then
    fail "missing compat header symbol: $sym"
  fi
  if ! grep -Fxq "$sym" "$runtime_symbols"; then
    fail "missing runtime object symbol: $sym ($runtime_obj)"
  fi
done <<EOF
$(required_symbols)
EOF

echo "verify_backend_runtime_abi ok"
