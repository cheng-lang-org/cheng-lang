#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
core_bin="$script_dir/../tooling_bundle/core/cheng_tooling_global"
full_bin="$script_dir/../tooling_bundle/full/cheng_tooling"
first_arg="${1:-}"

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

normalized_first="$(normalize_route_alias "$first_arg")"
if [ "$first_arg" != "" ] && [ "$normalized_first" != "$first_arg" ]; then
  shift || true
  set -- "$normalized_first" "$@"
  first_arg="$normalized_first"
fi

resolve_root() {
  start_dir="$1"
  cur="$start_dir"
  while [ "$cur" != "/" ] && [ "$cur" != "." ] && [ "$cur" != "" ]; do
    if [ -f "$cur/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh" ] && \
       [ -x "$cur/artifacts/tooling_cmd/cheng_tooling" ]; then
      printf '%s\n' "$cur"
      return 0
    fi
    cur="$(dirname -- "$cur")"
  done
  return 1
}

tooling_exec_is_repo_wrapper() {
  candidate="$1"
  if [ "$candidate" = "" ] || [ ! -f "$candidate" ]; then
    return 1
  fi
  first_line="$(sed -n '1p' "$candidate" 2>/dev/null || true)"
  case "$first_line" in
    '#!'*sh*)
      grep -Fxq 'exec sh "$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh" "$@"' "$candidate" 2>/dev/null
      return $?
      ;;
  esac
  return 1
}

script_is_self_trampoline() {
  subcmd="$1"
  script_path="$2"
  tr '\n' ' ' <"$script_path" 2>/dev/null | grep -Eq "[[:space:]]exec[[:space:]].*((TOOLING_SELF_BIN|\\\$tool)|cheng_tooling\\.sh).*[[:space:]]$subcmd([[:space:]]|$)"
}

is_native_only_repo_bypass() {
  case "${1:-}" in
    build-backend-driver|build_backend_driver|\
    verify_backend_noalias_opt|verify_backend_egraph_cost|verify_backend_dod_opt_regression)
      return 0
      ;;
  esac
  return 1
}

resolve_repo_script() {
  root="$1"
  subcmd="$2"
  if [ "$subcmd" = "" ] || [ "$subcmd" = "cheng_tooling" ]; then
    return 1
  fi
  if is_native_only_repo_bypass "$subcmd"; then
    return 1
  fi
  script_path="$root/src/tooling/cheng_tooling_embedded_scripts/$subcmd.sh"
  if [ ! -s "$script_path" ]; then
    return 1
  fi
  if script_is_self_trampoline "$subcmd" "$script_path"; then
    return 1
  fi
  printf '%s\n' "$script_path"
}

is_list_cmd() {
  case "${1:-}" in
    list|embedded-ids|embedded_ids) return 0 ;;
  esac
  return 1
}

tooling_lower_ascii() {
  printf '%s' "${1:-}" | tr '[:upper:]' '[:lower:]'
}

tooling_is_true() {
  case "$(tooling_lower_ascii "$(printf '%s' "${1:-}" | tr -d '[:space:]')")" in
    1|true|yes|on) return 0 ;;
  esac
  return 1
}

tooling_build_global_allow_sidecar_wrapper() {
  tooling_is_true "${TOOLING_BUILD_GLOBAL_ALLOW_SIDECAR_WRAPPER:-0}"
}

tooling_build_global_force_direct() {
  first_arg_local="${1:-}"
  shift || true
  case "$first_arg_local" in
    build-global|build_global) ;;
    *) return 1 ;;
  esac
  if [ "$(tooling_lower_ascii "${TOOLING_BUILD_GLOBAL_STAGE0_ROUTE:-}")" = "direct" ]; then
    return 0
  fi
  if tooling_is_true "${TOOLING_BUILD_GLOBAL_PREFER_CURRENTSRC_PROOF:-0}"; then
    return 0
  fi
  while [ "$#" -gt 0 ]; do
    case "$1" in
      --driver:*)
        if tooling_build_global_allow_sidecar_wrapper; then
          return 1
        fi
        return 0
        ;;
    esac
    shift
  done
  return 1
}

tooling_resolve_strict_sidecar_contract() {
  return 1
}

run_one() {
  bin="$1"
  shift || true
  [ -x "$bin" ] || return 127
  if tooling_exec_is_repo_wrapper "$bin"; then
    printf '%s\n' "[cheng_tooling.real.bin] refusing repo-wrapper trampoline candidate: $bin" 1>&2
    return 127
  fi
  if tooling_build_global_force_direct "$@"; then
    env TOOLING_SELF_BIN="$bin" TOOLING_BUILD_GLOBAL_STAGE0_ROUTE=direct "$bin" "$@"
    return $?
  fi
  env TOOLING_SELF_BIN="$bin" "$bin" "$@"
}

tooling_rc_reason() {
  case "${1:-}" in
    126) printf '%s\n' "not_executable" ;;
    127) printf '%s\n' "missing_executable" ;;
    139) printf '%s\n' "segfault" ;;
    223) printf '%s\n' "deterministic_exit_223" ;;
    *) printf '%s\n' "rc_${1:-unknown}" ;;
  esac
}

tooling_print_rc_hint() {
  rc="${1:-}"
  if [ "$rc" = "223" ]; then
    printf '%s\n' "[cheng_tooling.real.bin] deterministic_exit_223: child exited 223 directly (non-POSIX signal)" 1>&2
  fi
}

run_with_fallback() {
  first="$1"
  second="$2"
  shift 2 || true
  if tooling_exec_is_repo_wrapper "$first"; then
    run_one "$second" "$@"
    return $?
  fi
  if [ ! -x "$first" ]; then
    run_one "$second" "$@"
    return $?
  fi
  set +e
  run_one "$first" "$@"
  rc=$?
  set -e
  tooling_print_rc_hint "$rc"
  case "$rc" in
    0)
      return 0
      ;;
    126|127|139)
      if [ -x "$second" ]; then
        printf '%s\n' "[cheng_tooling.real.bin] fallback: $first -> $second (rc=$rc reason=$(tooling_rc_reason "$rc"))" 1>&2
        run_one "$second" "$@"
        return $?
      fi
      ;;
  esac
  return "$rc"
}

root="$(resolve_root "$script_dir" || true)"
repo_wrapper=""
if [ "$root" != "" ]; then
  repo_wrapper="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh"
  if [ -x "$root/artifacts/tooling_bundle/core/cheng_tooling_global" ]; then
    core_bin="$root/artifacts/tooling_bundle/core/cheng_tooling_global"
  fi
  if [ -x "$root/artifacts/tooling_bundle/full/cheng_tooling" ]; then
    full_bin="$root/artifacts/tooling_bundle/full/cheng_tooling"
  fi
fi

if [ "$repo_wrapper" != "" ] && [ "$(resolve_repo_script "$root" "$first_arg" || true)" != "" ]; then
  exec env TOOLING_BIN="$core_bin" TOOLING_SELF_BIN="$core_bin" sh "$repo_wrapper" "$@"
fi

if [ "$repo_wrapper" != "" ] && is_list_cmd "$first_arg"; then
  exec env TOOLING_BIN="$core_bin" TOOLING_SELF_BIN="$core_bin" TOOLING_LIST_BIN="$core_bin" sh "$repo_wrapper" "$@"
fi

if [ "$repo_wrapper" != "" ] && [ "$first_arg" = "install" ]; then
  exec env \
    TOOLING_BIN="$core_bin" \
    TOOLING_SELF_BIN="$core_bin" \
    TOOLING_INSTALL_DEFAULT_BIN="$root/artifacts/tooling_cmd/cheng_tooling.real.bin" \
    TOOLING_INSTALL_TOOL_BIN="$core_bin" \
    sh "$repo_wrapper" "$@"
fi

if [ "$repo_wrapper" != "" ] && [ "$first_arg" = "bundle" ]; then
  exec env \
    TOOLING_BIN="$core_bin" \
    TOOLING_SELF_BIN="$core_bin" \
    TOOLING_BUNDLE_TOOL_BIN="$core_bin" \
    sh "$repo_wrapper" "$@"
fi

run_with_fallback "$core_bin" "$full_bin" "$@"
exit $?
