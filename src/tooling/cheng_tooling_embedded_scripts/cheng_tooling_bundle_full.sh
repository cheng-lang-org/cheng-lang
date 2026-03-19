#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

resolve_root() {
  start_dir="$1"
  cur="$start_dir"
  while [ "$cur" != "/" ] && [ "$cur" != "." ] && [ "$cur" != "" ]; do
    if [ -f "$cur/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh" ]; then
      printf '%s\n' "$cur"
      return 0
    fi
    cur="$(dirname -- "$cur")"
  done
  return 1
}

normalize_route_alias() {
  case "${1:-}" in
    sync-global) printf '%s\n' "sync_global" ;;
    build-backend-rawptr-contract) printf '%s\n' "build_backend_rawptr_contract" ;;
    build-backend-mem-contract) printf '%s\n' "build_backend_mem_contract" ;;
    build-backend-dod-contract) printf '%s\n' "build_backend_dod_contract" ;;
    build-backend-determinism-contract) printf '%s\n' "build_backend_determinism_contract" ;;
    build-backend-profile-schema) printf '%s\n' "build_backend_profile_schema" ;;
    build-backend-profile-baseline) printf '%s\n' "build_backend_profile_baseline" ;;
    build-backend-string-abi-contract) printf '%s\n' "build_backend_string_abi_contract" ;;
    build-backend-dot-lowering-contract) printf '%s\n' "build_backend_dot_lowering_contract" ;;
    backend-prod-publish) printf '%s\n' "backend_prod_publish" ;;
    backend-resolve-selflink-runtime-obj) printf '%s\n' "backend_resolve_selflink_runtime_obj" ;;
    selfhost-100ms-host) printf '%s\n' "verify_backend_selfhost_100ms_host" ;;
    infant-speak) printf '%s\n' "infant_speak" ;;
    stage0-ue-status) printf '%s\n' "stage0_ue_status" ;;
    stage0-ue-clean) printf '%s\n' "stage0_ue_clean" ;;
    *) printf '%s\n' "${1:-}" ;;
  esac
}

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
root="$(resolve_root "$script_dir" || true)"

if [ "$root" = "" ]; then
  printf '%s\n' "[cheng_tooling.bundle.full] failed to resolve repo root from $script_dir" 1>&2
  exit 1
fi

core_bin="$root/artifacts/tooling_bundle/core/cheng_tooling_global"
if [ ! -x "$core_bin" ]; then
  printf '%s\n' "[cheng_tooling.bundle.full] missing core tooling binary: $core_bin" 1>&2
  exit 1
fi

first_arg="${1:-}"
normalized_first="$(normalize_route_alias "$first_arg")"
if [ "$first_arg" != "" ] && [ "$normalized_first" != "$first_arg" ]; then
  shift || true
  set -- "$normalized_first" "$@"
fi

exec "$core_bin" "$@"
