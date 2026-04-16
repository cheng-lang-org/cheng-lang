#!/usr/bin/env sh
:

backend_runtime_abi_contract_abs() {
  path="$1"
  case "$path" in
    /*) printf '%s\n' "$path" ;;
    *) printf '%s\n' "$root/$path" ;;
  esac
}

backend_runtime_abi_contract_load() {
  if [ "${BACKEND_RUNTIME_ABI_CONTRACT_LOADED:-0}" = "1" ]; then
    return 0
  fi
  if [ "${root:-}" = "" ]; then
    root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
  fi
  contract_file="$root/src/tooling/backend_runtime_abi_contract.env"
  if [ ! -f "$contract_file" ]; then
    echo "[backend_runtime_abi_contract] missing contract file: $contract_file" 1>&2
    return 1
  fi
  # shellcheck disable=SC1090
  . "$contract_file"
  BACKEND_RUNTIME_ABI_AUTHORITY_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_AUTHORITY")"
  BACKEND_RUNTIME_ABI_COMPAT_HEADER_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_COMPAT_HEADER")"
  BACKEND_RUNTIME_ABI_BRIDGE_SHIM_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_BRIDGE_SHIM")"
  BACKEND_RUNTIME_ABI_BRIDGE_IOTIME_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_BRIDGE_IOTIME")"
  BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI")"
  BACKEND_RUNTIME_ABI_BRIDGE_RAW_FFI_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_BRIDGE_RAW_FFI")"
  BACKEND_RUNTIME_ABI_MOBILE_STUB_C_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_MOBILE_STUB_C")"
  BACKEND_RUNTIME_ABI_MOBILE_STUB_H_ABS="$(backend_runtime_abi_contract_abs "$BACKEND_RUNTIME_ABI_MOBILE_STUB_H")"
  BACKEND_RUNTIME_ABI_CONTRACT_LOADED=1
}

backend_runtime_abi_contract_each_core_authority() {
  printf '%s\n' \
    "$BACKEND_RUNTIME_ABI_AUTHORITY" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_SHIM" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_IOTIME" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI"
}

backend_runtime_abi_contract_each_core_authority_abs() {
  printf '%s\n' \
    "$BACKEND_RUNTIME_ABI_AUTHORITY_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_SHIM_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_IOTIME_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI_ABS"
}

backend_runtime_abi_contract_each_runtime_bridge() {
  printf '%s\n' \
    "$BACKEND_RUNTIME_ABI_BRIDGE_SHIM" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_IOTIME" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI"
}

backend_runtime_abi_contract_each_runtime_bridge_abs() {
  printf '%s\n' \
    "$BACKEND_RUNTIME_ABI_BRIDGE_SHIM_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_IOTIME_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI_ABS"
}

backend_runtime_abi_contract_each_ffi_authority() {
  printf '%s\n' \
    "$BACKEND_RUNTIME_ABI_AUTHORITY" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_RAW_FFI"
}

backend_runtime_abi_contract_each_ffi_authority_abs() {
  printf '%s\n' \
    "$BACKEND_RUNTIME_ABI_AUTHORITY_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_RAW_FFI_ABS"
}

backend_runtime_abi_contract_require_core_files() {
  backend_runtime_abi_contract_load || return 1
  for file in \
    "$BACKEND_RUNTIME_ABI_AUTHORITY_ABS" \
    "$BACKEND_RUNTIME_ABI_COMPAT_HEADER_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_SHIM_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_IOTIME_ABS" \
    "$BACKEND_RUNTIME_ABI_BRIDGE_HOST_PROCESS_FFI_ABS"; do
    [ -f "$file" ] || return 1
  done
  return 0
}
