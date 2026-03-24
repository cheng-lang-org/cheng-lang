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

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
root="$(resolve_root "$script_dir" || true)"
real_bin=""
repo_wrapper=""
self_name="$(basename -- "$0")"
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

if [ "$root" != "" ]; then
  real_bin="$root/artifacts/tooling_cmd/cheng_tooling.real"
  repo_wrapper="$root/src/tooling/cheng_tooling_embedded_scripts/cheng_tooling.sh"
fi

if [ "$root" = "" ]; then
  echo "[cheng_tooling] missing real binary: $real_bin" 1>&2
  exit 1
fi

scripts_dir="$root/src/tooling/cheng_tooling_embedded_scripts"
canonical_wrapper_src="$scripts_dir/cheng_tooling_canonical.sh"
canonical_wrapper_bin="$root/artifacts/tooling_cmd/cheng_tooling"
real_launcher_src="$scripts_dir/cheng_tooling_real.sh"
real_primary_src="$scripts_dir/cheng_tooling_real_bin.sh"
real_primary_bin="$root/artifacts/tooling_cmd/cheng_tooling.real.bin"

tooling_path_abs() {
  path_raw="$1"
  case "$path_raw" in
    /*) printf '%s\n' "$path_raw" ;;
    *) printf '%s\n' "$root/$path_raw" ;;
  esac
}

tooling_lower_ascii() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]'
}

sync_tooling_exec_template() {
  src_path="$1"
  dst_path="$2"
  if [ ! -f "$src_path" ]; then
    return 1
  fi
  mkdir -p "$(dirname "$dst_path")"
  if [ -f "$dst_path" ] && cmp -s "$src_path" "$dst_path"; then
    chmod +x "$dst_path" 2>/dev/null || true
    return 2
  fi
  cp "$src_path" "$dst_path"
  chmod +x "$dst_path"
  return 0
}

run_build_global_canonical_sync() {
  first_arg="${1:-}"
  case "$first_arg" in
    build-global|build_global) ;;
    *) return 1 ;;
  esac
  shift || true
  canonical_out="$canonical_wrapper_bin"
  while [ "$#" -gt 0 ]; do
    arg="$1"
    case "$arg" in
      --out:*)
        canonical_out="$(tooling_path_abs "${arg#--out:}")"
        ;;
      --linker:*)
        linker_value="$(tooling_lower_ascii "$(printf '%s' "${arg#--linker:}" | tr -d '[:space:]')")"
        if [ "$linker_value" != "self" ]; then
          return 1
        fi
        ;;
      --driver:*)
        ;;
      --help|-h)
        return 1
        ;;
      *)
        return 1
        ;;
    esac
    shift
  done
  if [ "$canonical_out" != "$canonical_wrapper_bin" ]; then
    return 1
  fi
  changed=0
  sync_tooling_exec_template "$canonical_wrapper_src" "$canonical_wrapper_bin"
  sync_rc="$?"
  if [ "$sync_rc" -eq 0 ]; then
    changed=1
  elif [ "$sync_rc" -ne 2 ]; then
    printf '%s\n' "cheng_tooling: build-global missing canonical wrapper template" 1>&2
    return 2
  fi
  sync_tooling_exec_template "$real_launcher_src" "$real_bin"
  sync_rc="$?"
  if [ "$sync_rc" -eq 0 ]; then
    changed=1
  elif [ "$sync_rc" -ne 2 ]; then
    printf '%s\n' "cheng_tooling: build-global missing real launcher template" 1>&2
    return 2
  fi
  sync_tooling_exec_template "$real_primary_src" "$real_primary_bin"
  sync_rc="$?"
  if [ "$sync_rc" -eq 0 ]; then
    changed=1
  elif [ "$sync_rc" -ne 2 ]; then
    printf '%s\n' "cheng_tooling: build-global missing real primary template" 1>&2
    return 2
  fi
  binary_surface_ok=0
  if [ -x "$canonical_wrapper_bin" ] && [ -x "$real_bin" ] && [ -x "$real_primary_bin" ]; then
    if "$canonical_wrapper_bin" list >/dev/null 2>&1; then
      binary_surface_ok=1
    fi
  fi
  printf '%s\n' "build_global_mode=canonical_wrapper_sync"
  printf '%s\n' "build_global_out=$canonical_wrapper_bin"
  printf '%s\n' "build_global_release_fallback=0"
  printf '%s\n' "build_global_release_fallback_allowed=0"
  printf '%s\n' "build_global_rebuilt=$changed"
  printf '%s\n' "build_global_binary_surface_ok=$binary_surface_ok"
  if [ "$binary_surface_ok" = "1" ]; then
    printf '%s\n' "build_global_status=ok"
    printf '%s\n' "build_global_rc=0"
    return 0
  fi
  printf '%s\n' "build_global_status=failed"
  printf '%s\n' "build_global_rc=1"
  return 2
}

script_is_self_trampoline() {
  subcmd="$1"
  script_path="$2"
  tr '\n' ' ' <"$script_path" 2>/dev/null | grep -Eq "[[:space:]]exec[[:space:]].*((TOOLING_SELF_BIN|\\\$tool)|cheng_tooling\\.sh).*[[:space:]]$subcmd([[:space:]]|$)"
}

resolve_repo_script() {
  subcmd="$1"
  if [ "$subcmd" = "" ] || [ "$subcmd" = "cheng_tooling" ]; then
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

run_repo_wrapper() {
  exec env \
    TOOLING_BIN="$real_bin" \
    TOOLING_SELF_BIN="$real_primary_bin" \
    TOOLING_LIST_BIN="$root/artifacts/tooling_bundle/core/cheng_tooling_global" \
    sh "$repo_wrapper" "$@"
}

if [ "$self_name" != "cheng_tooling" ]; then
  repo_script_self="$(resolve_repo_script "$self_name" || true)"
  if [ "$repo_script_self" != "" ]; then
    exec env \
      TOOLING_ROOT="$root" \
      TOOLING_SELF_BIN="$canonical_wrapper_bin" \
      sh "$repo_script_self" "$@"
  fi
  if [ ! -x "$real_bin" ]; then
    echo "[cheng_tooling] missing real binary: $real_bin" 1>&2
    exit 1
  fi
  exec "$real_bin" "$self_name" "$@"
fi

if [ "$(resolve_repo_script "$first_arg" || true)" != "" ]; then
  run_repo_wrapper "$@"
fi

if run_build_global_canonical_sync "$@"; then
  exit 0
fi

if is_list_cmd "$first_arg"; then
  run_repo_wrapper "$@"
fi

if [ "$first_arg" = "install" ]; then
  run_repo_wrapper "$@"
fi

if [ "$first_arg" = "bundle" ]; then
  run_repo_wrapper "$@"
fi

if [ ! -x "$real_bin" ]; then
  echo "[cheng_tooling] missing real binary: $real_bin" 1>&2
  exit 1
fi

exec "$real_bin" "$@"
