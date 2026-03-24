#!/usr/bin/env sh
:

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
  script_path="$scripts_dir/$subcmd.sh"
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

list_repo_script_ids() {
  for script_path in "$scripts_dir"/*.sh; do
    [ -e "$script_path" ] || continue
    subcmd="$(basename -- "$script_path" .sh)"
    if [ "$(resolve_repo_script "$subcmd" || true)" != "" ]; then
      printf '%s\n' "$subcmd"
    fi
  done | LC_ALL=C sort -u
}

selector_append() {
  selector="$1"
  value="$2"
  if [ "$selector" = "" ]; then
    printf '%s\n' "$value"
  else
    printf '%s,%s\n' "$selector" "$value"
  fi
}

selector_contains() {
  selector="$1"
  value="$2"
  if [ "$selector" = "" ]; then
    return 1
  fi
  old_ifs="${IFS- }"
  IFS=','
  set -- $selector
  IFS="$old_ifs"
  for item in "$@"; do
    if [ "$item" = "$value" ]; then
      return 0
    fi
  done
  return 1
}

repo_gate_support_ids() {
  for gate_id in \
    verify_backend_stage1_fixed0_envs \
    verify_backend_string_literal_regression \
    verify_backend_macho_cstring_overlap \
    verify_backend_sidecar_cheng_fresh \
    verify_backend_default_output_safety \
    verify_new_expr_surface \
    verify_backend_pure_cheng_surface \
    verify_backend_dod_soa \
    verify_backend_mir_borrow \
    verify_backend_string_abi_contract \
    verify_backend_dot_lowering_contract \
    verify_backend_selfhost_currentsrc_proof; do
    printf '%s\n' "$gate_id"
  done
}

native_cli_support_ids() {
  for gate_id in \
    backend_resolve_selflink_runtime_obj \
    bootstrap \
    verify_backend_cdrop_emergency \
    verify_std_layout_sync \
    verify_std_strformat \
    verify_std_foundation \
    verify_std_string_json \
    verify_std_file_io \
    verify_std_net_io \
    verify_std_baseline \
    verify_std_docs_examples \
    verify_std_stability \
    build_std_perf_baseline \
    verify_std_perf_baseline; do
    printf '%s\n' "$gate_id"
  done
}

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
  sync_tooling_exec_template "$real_launcher_src" "$real_launcher_bin"
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
  if [ -x "$canonical_wrapper_bin" ] && [ -x "$real_launcher_bin" ] && [ -x "$real_primary_bin" ]; then
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

install_support_repo_script_gate() {
  install_dir="$1"
  manifest_path="$2"
  only_selector="${3:-}"
  exclude_selector="${4:-}"
  target_bin="$5"
  for gate_id in $(repo_gate_support_ids); do
    gate_script="$(resolve_repo_script "$gate_id" || true)"
    if [ "$gate_script" = "" ]; then
      continue
    fi
    if [ "$only_selector" != "" ] && ! selector_contains "$only_selector" "$gate_id"; then
      continue
    fi
    if selector_contains "$exclude_selector" "$gate_id"; then
      continue
    fi
    link_path="$install_dir/$gate_id"
    if [ "$gate_id" = "verify_backend_mir_borrow" ]; then
      cat >"$link_path" <<EOF
#!/usr/bin/env sh
export TOOLING_ROOT="$root"
export TOOLING_SELF_BIN="$target_bin"
exec sh "$gate_script" "\$@"
EOF
    else
      cat >"$link_path" <<EOF
#!/usr/bin/env sh
exec "$target_bin" "$gate_id" "\$@"
EOF
    fi
    chmod +x "$link_path"
    if [ "$manifest_path" != "" ]; then
      if [ ! -f "$manifest_path" ]; then
        printf 'name\tlink\n' >"$manifest_path"
      fi
      if ! grep -qx "$gate_id	$link_path" "$manifest_path" 2>/dev/null; then
        printf '%s\t%s\n' "$gate_id" "$link_path" >>"$manifest_path"
      fi
    fi
  done
}

install_support_native_cli_gate() {
  install_dir="$1"
  manifest_path="$2"
  only_selector="${3:-}"
  exclude_selector="${4:-}"
  target_bin="$5"
  for gate_id in $(native_cli_support_ids); do
    if [ "$only_selector" != "" ] && ! selector_contains "$only_selector" "$gate_id"; then
      continue
    fi
    if selector_contains "$exclude_selector" "$gate_id"; then
      continue
    fi
    link_path="$install_dir/$gate_id"
    cat >"$link_path" <<EOF
#!/usr/bin/env sh
exec "$target_bin" "$gate_id" "\$@"
EOF
    chmod +x "$link_path"
    if [ "$manifest_path" != "" ]; then
      if [ ! -f "$manifest_path" ]; then
        printf 'name\tlink\n' >"$manifest_path"
      fi
      if ! grep -qx "$gate_id	$link_path" "$manifest_path" 2>/dev/null; then
        printf '%s\t%s\n' "$gate_id" "$link_path" >>"$manifest_path"
      fi
    fi
  done
}

bundle_support_repo_script_gate() {
  bundle_dir="$1"
  bundle_manifest_path="$2"
  for gate_id in $(repo_gate_support_ids); do
    gate_script="$(resolve_repo_script "$gate_id" || true)"
    if [ "$gate_script" = "" ]; then
      continue
    fi
    link_path="$bundle_dir/$gate_id"
    rm -f "$link_path"
    awk '
      BEGIN { replaced = 0 }
      !replaced && $0 ~ /^root="/ {
        print "root=\"$(CDPATH= cd -- \"$(dirname -- \"$0\")/../../../..\" && pwd)\""
        replaced = 1
        next
      }
      { print }
    ' "$gate_script" >"$link_path"
    chmod +x "$link_path"
    if [ "$bundle_manifest_path" != "" ]; then
      if [ ! -f "$bundle_manifest_path" ]; then
        printf 'name\tlink\n' >"$bundle_manifest_path"
      fi
      if ! grep -qx "$gate_id	$link_path" "$bundle_manifest_path" 2>/dev/null; then
        printf '%s\t%s\n' "$gate_id" "$link_path" >>"$bundle_manifest_path"
      fi
    fi
  done
}

bundle_support_native_cli_gate() {
  bundle_dir="$1"
  bundle_manifest_path="$2"
  for gate_id in $(native_cli_support_ids); do
    link_path="$bundle_dir/$gate_id"
    cat >"$link_path" <<EOF
#!/usr/bin/env sh
tool_dir="\$(CDPATH= cd -- "\$(dirname -- "\$0")" && pwd)"
exec "\$tool_dir/cheng_tooling" "$gate_id" "\$@"
EOF
    chmod +x "$link_path"
    if [ "$bundle_manifest_path" != "" ]; then
      if [ ! -f "$bundle_manifest_path" ]; then
        printf 'name\tlink\n' >"$bundle_manifest_path"
      fi
      if ! grep -qx "$gate_id	$link_path" "$bundle_manifest_path" 2>/dev/null; then
        printf '%s\t%s\n' "$gate_id" "$link_path" >>"$bundle_manifest_path"
      fi
    fi
  done
}

run_augmented_install() {
  install_dir="$root/artifacts/tooling_cmd/bin"
  manifest=""
  target_bin="${TOOLING_INSTALL_DEFAULT_BIN:-}"
  install_tool_bin="${TOOLING_INSTALL_TOOL_BIN:-}"
  only_selector=""
  core_only_selector=""
  exclude_selector=""
  mode=""
  force="0"
  help_mode="0"
  i=1
  while [ "$i" -le "$#" ]; do
    eval "arg=\${$i}"
    case "$arg" in
      --dir:*)
        install_dir="$(tooling_path_abs "${arg#--dir:}")"
        ;;
      --manifest:*)
        manifest="$(tooling_path_abs "${arg#--manifest:}")"
        ;;
      --bin:*)
        target_bin="$(tooling_path_abs "${arg#--bin:}")"
        ;;
      --mode:*)
        mode="${arg#--mode:}"
        ;;
      --only:*)
        gate_id="${arg#--only:}"
        only_selector="$(selector_append "$only_selector" "$gate_id")"
        if [ "$(resolve_repo_script "$gate_id" || true)" = "" ]; then
          core_only_selector="$(selector_append "$core_only_selector" "$gate_id")"
        fi
        ;;
      --exclude:*)
        exclude_selector="$(selector_append "$exclude_selector" "${arg#--exclude:}")"
        ;;
      --force)
        force="1"
        ;;
      --force:*)
        force="${arg#--force:}"
        ;;
      --help|-h)
        help_mode="1"
        ;;
    esac
    i=$((i + 1))
  done
  if [ "$target_bin" = "" ]; then
    if [ -x "$root/artifacts/tooling_cmd/cheng_tooling" ]; then
      target_bin="$root/artifacts/tooling_cmd/cheng_tooling"
    else
      target_bin="$bin"
    fi
  else
    target_bin="$(tooling_path_abs "$target_bin")"
  fi
  if [ "$install_tool_bin" = "" ]; then
    if [ -x "$core_bin" ]; then
      install_tool_bin="$core_bin"
    else
      install_tool_bin="$bin"
    fi
  fi
  install_rc=0
  if [ "$help_mode" = "1" ]; then
    "$install_tool_bin" "$@"
    install_rc="$?"
    if [ "$install_rc" -ne 0 ]; then
      return "$install_rc"
    fi
  else
    core_should_run=1
    if [ "$only_selector" != "" ] && [ "$core_only_selector" = "" ]; then
      core_should_run=0
    fi
    if [ "$core_should_run" = "1" ]; then
      core_exclude_selector="$exclude_selector"
      for gate_id in $(repo_gate_support_ids); do
        if [ "$(resolve_repo_script "$gate_id" || true)" = "" ]; then
          continue
        fi
        if [ "$only_selector" != "" ] && ! selector_contains "$only_selector" "$gate_id"; then
          continue
        fi
        if selector_contains "$core_exclude_selector" "$gate_id"; then
          continue
        fi
        core_exclude_selector="$(selector_append "$core_exclude_selector" "$gate_id")"
      done
      set -- install "--dir:$install_dir"
      if [ "$manifest" != "" ]; then
        set -- "$@" "--manifest:$manifest"
      fi
      if [ "$target_bin" != "" ]; then
        set -- "$@" "--bin:$target_bin"
      fi
      if [ "$mode" != "" ]; then
        set -- "$@" "--mode:$mode"
      fi
      if [ "$force" = "1" ] || [ "$force" = "true" ]; then
        set -- "$@" "--force"
      fi
      if [ "$core_only_selector" != "" ]; then
        set -- install "--dir:$install_dir"
        if [ "$manifest" != "" ]; then
          set -- "$@" "--manifest:$manifest"
        fi
        if [ "$target_bin" != "" ]; then
          set -- "$@" "--bin:$target_bin"
        fi
        if [ "$mode" != "" ]; then
          set -- "$@" "--mode:$mode"
        fi
        if [ "$force" = "1" ] || [ "$force" = "true" ]; then
          set -- "$@" "--force"
        fi
        old_ifs="${IFS- }"
        IFS=','
        for gate_id in $core_only_selector; do
          [ "$gate_id" = "" ] && continue
          set -- "$@" "--only:$gate_id"
        done
        IFS="$old_ifs"
      fi
      if [ "$core_exclude_selector" != "" ]; then
        old_ifs="${IFS- }"
        IFS=','
        for gate_id in $core_exclude_selector; do
          [ "$gate_id" = "" ] && continue
          set -- "$@" "--exclude:$gate_id"
        done
        IFS="$old_ifs"
      fi
      "$install_tool_bin" "$@"
      install_rc="$?"
      if [ "$install_rc" -ne 0 ]; then
        return "$install_rc"
      fi
    fi
  fi
  mkdir -p "$install_dir"
  install_support_repo_script_gate "$install_dir" "$manifest" "$only_selector" "$exclude_selector" "$target_bin"
  install_support_native_cli_gate "$install_dir" "$manifest" "$only_selector" "$exclude_selector" "$target_bin"
}

run_augmented_bundle() {
  bundle_out_dir="$root/artifacts/tooling_bundle"
  bundle_tool_bin="${TOOLING_BUNDLE_TOOL_BIN:-}"
  bundle_driver="${TOOLING_BUNDLE_DRIVER:-}"
  bundle_wrapper_src="${TOOLING_BUNDLE_FULL_WRAPPER_SRC:-${full_bundle_src:-$scripts_dir/cheng_tooling_bundle_full.sh}}"
  bundle_has_driver="0"
  i=1
  while [ "$i" -le "$#" ]; do
    eval "arg=\${$i}"
    case "$arg" in
      --out-dir:*)
        bundle_out_dir="$(tooling_path_abs "${arg#--out-dir:}")"
        ;;
      --driver:*)
        bundle_driver="$(tooling_path_abs "${arg#--driver:}")"
        bundle_has_driver="1"
        ;;
    esac
    i=$((i + 1))
  done
  if [ "$bundle_tool_bin" = "" ]; then
    if [ -x "$core_bin" ]; then
      bundle_tool_bin="$core_bin"
    else
      bundle_tool_bin="$bin"
    fi
  fi
  if [ "$bundle_driver" = "" ]; then
    if [ -x "$root/artifacts/backend_driver/cheng" ]; then
      bundle_driver="$root/artifacts/backend_driver/cheng"
    elif [ -x "$root/artifacts/backend_selfhost_self_obj/cheng.stage2" ]; then
      bundle_driver="$root/artifacts/backend_selfhost_self_obj/cheng.stage2"
    elif [ -x "$root/artifacts/backend_seed/cheng.stage2" ]; then
      bundle_driver="$root/artifacts/backend_seed/cheng.stage2"
    else
      bundle_driver="$("$bundle_tool_bin" backend_driver_path)"
    fi
  fi
  if [ "$bundle_has_driver" = "0" ]; then
    set -- "$@" "--driver:$bundle_driver"
  fi
  "$bundle_tool_bin" "$@"
  bundle_rc="$?"
  if [ "$bundle_rc" -ne 0 ]; then
    return "$bundle_rc"
  fi
  bundle_profile_dir="$bundle_out_dir/full"
  bundle_bin_path="$bundle_profile_dir/cheng_tooling"
  bundle_links_dir="$bundle_profile_dir/bin"
  bundle_link_tool_path="$bundle_links_dir/cheng_tooling"
  bundle_manifest_path="$bundle_profile_dir/manifest.tsv"
  if [ -x "$bundle_bin_path" ] && [ -d "$bundle_links_dir" ]; then
    rm -f "$bundle_bin_path" "$bundle_link_tool_path"
    sync_tooling_exec_template "$bundle_wrapper_src" "$bundle_bin_path" >/dev/null 2>&1 || true
    sync_tooling_exec_template "$bundle_wrapper_src" "$bundle_link_tool_path" >/dev/null 2>&1 || true
    bundle_support_repo_script_gate "$bundle_links_dir" "$bundle_manifest_path"
    bundle_support_native_cli_gate "$bundle_links_dir" "$bundle_manifest_path"
  fi
}

run_augmented_list() {
  base_bin="$list_bin"
  if [ "$base_bin" = "" ]; then
    if [ -x "$root/artifacts/tooling_bundle/core/cheng_tooling_global" ]; then
      base_bin="$root/artifacts/tooling_bundle/core/cheng_tooling_global"
    elif [ -x "$real_primary_bin" ]; then
      base_bin="$real_primary_bin"
    elif [ -x "$real_launcher_bin" ]; then
      base_bin="$real_launcher_bin"
    else
      base_bin="$bin"
    fi
  fi
  tmp_out="$(mktemp "${TMPDIR:-/tmp}/cheng_tooling.list.XXXXXX")"
  trap 'rm -f "$tmp_out"' EXIT INT TERM
  "$base_bin" "$@" >"$tmp_out"
  cat "$tmp_out"
  list_repo_script_ids | while IFS= read -r id; do
    if ! grep -qx "$id" "$tmp_out"; then
      printf '%s\n' "$id"
    fi
  done
  native_cli_support_ids | while IFS= read -r id; do
    if ! grep -qx "$id" "$tmp_out"; then
      printf '%s\n' "$id"
    fi
  done
  rm -f "$tmp_out"
  trap - EXIT INT TERM
}
