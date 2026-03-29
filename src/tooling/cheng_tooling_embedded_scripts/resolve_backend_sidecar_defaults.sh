#!/usr/bin/env sh
:
set -eu
(set -o pipefail) 2>/dev/null && set -o pipefail

usage() {
  cat <<'USAGE'
Usage:
  src/tooling/cheng_tooling_embedded_scripts/resolve_backend_sidecar_defaults.sh --field:mode|bundle|compiler|driver|real_driver|child_mode|outer_companion [--target:<triple>] [--root:<path>]

Resolves the default backend UIR sidecar contract from the fresh Cheng sidecar gate.
Pure Cheng sidecar is strict-fresh only: missing or stale fresh snapshot is an error.
USAGE
}

detect_host_target() {
  host_os="$(uname -s 2>/dev/null || echo unknown)"
  host_arch="$(uname -m 2>/dev/null || echo unknown)"
  case "$host_os/$host_arch" in
    Darwin/arm64)
      printf '%s\n' "arm64-apple-darwin"
      return 0
      ;;
    Linux/aarch64|Linux/arm64)
      printf '%s\n' "aarch64-unknown-linux-gnu"
      return 0
      ;;
  esac
  printf '%s\n' ""
  return 0
}

strict_stage0_lineage_dir() {
  printf '%s\n' "$root/artifacts/backend_selfhost_self_obj/probe_currentsrc_proof"
}

strict_stage0_meta_field() {
  meta_path="$1"
  key="$2"
  if [ ! -f "$meta_path" ]; then
    printf '\n'
    return 0
  fi
  awk -F= -v key="$key" '$1 == key { print substr($0, index($0, "=") + 1); exit }' "$meta_path"
}

strict_stage0_published_surface() {
  driver_path="$1"
  case "$driver_path" in
    "$(strict_stage0_lineage_dir)"/cheng.stage1|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2|\
    "$(strict_stage0_lineage_dir)"/cheng.stage2.proof)
      return 0
      ;;
  esac
  return 1
}

strict_stage0_bootstrap_surface() {
  driver_path="$1"
  case "$driver_path" in
    "$(strict_stage0_lineage_dir)"/cheng_stage0_currentsrc.proof)
      return 0
      ;;
  esac
  return 1
}

strict_stage0_expected_label() {
  driver_path="$1"
  case "$driver_path" in
    */cheng_stage0_currentsrc.proof)
      printf '%s\n' "currentsrc.proof.bootstrap"
      return 0
      ;;
    */cheng.stage1)
      printf '%s\n' "stage1"
      return 0
      ;;
    */cheng.stage2)
      printf '%s\n' "stage2"
      return 0
      ;;
    */cheng.stage2.proof)
      printf '%s\n' "stage2.proof"
      return 0
      ;;
  esac
  printf '\n'
}

strict_stage0_expected_input() {
  driver_path="$1"
  case "$(strict_stage0_expected_label "$driver_path")" in
    currentsrc.proof.bootstrap)
      printf '%s\n' "src/backend/tooling/backend_driver_proof.cheng"
      return 0
      ;;
    stage1|stage2|stage2.proof)
      printf '%s\n' "src/backend/tooling/backend_driver.cheng"
      return 0
      ;;
  esac
  printf '\n'
}

strict_stage0_direct_export_driver_path() {
  driver_path="$1"
  case "$driver_path" in
    *.proof)
      outer_driver="$(strict_stage0_meta_field "${driver_path}.meta" "outer_driver")"
      if [ "$outer_driver" != "" ]; then
        printf '%s\n' "$outer_driver"
        return 0
      fi
      ;;
  esac
  printf '%s\n' "$driver_path"
}

strict_stage0_direct_export_surface_ok() {
  driver_path="$1"
  export_driver="$(strict_stage0_direct_export_driver_path "$driver_path")"
  [ "$export_driver" != "" ] || return 1
  [ -x "$export_driver" ] || return 1
  command -v nm >/dev/null 2>&1 || return 1
  command -v awk >/dev/null 2>&1 || return 1
  surface_log="$(mktemp "${TMPDIR:-/tmp}/strict_stage0_nm.XXXXXX")"
  set +e
  (nm -gU "$export_driver" 2>/dev/null || nm "$export_driver" 2>/dev/null) >"$surface_log"
  nm_status="$?"
  set -e
  if [ "$nm_status" -ne 0 ]; then
    rm -f "$surface_log"
    return 1
  fi
  if ! awk '
    BEGIN { need1=0; need2=0; need3=0; legacy=0; }
    {
      sym=$NF;
      gsub(/^_+/, "", sym);
      if (sym == "driver_export_build_emit_obj_from_file_stage1_target_impl") need1=1;
      if (sym == "driver_export_prefer_sidecar_builds") need2=1;
      if (sym == "driver_export_buildModuleFromFileStage1TargetRetained") need3=1;
      if (sym ~ /^driverProof/) legacy=1;
    }
    END { exit((need1 && need2 && need3 && !legacy) ? 0 : 1); }
  ' "$surface_log"; then
    rm -f "$surface_log"
    return 1
  fi
  rm -f "$surface_log"
  return 0
}

strict_stage0_meta_ok() {
  driver_path="$1"
  if ! strict_stage0_published_surface "$driver_path" && ! strict_stage0_bootstrap_surface "$driver_path"; then
    return 1
  fi
  meta_path="${driver_path}.meta"
  expected_label="$(strict_stage0_expected_label "$driver_path")"
  [ "$expected_label" != "" ] || return 1
  [ -f "$meta_path" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "meta_contract_version")" = "2" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "label")" = "$expected_label" ] || return 1
  meta_outer_driver="$(strict_stage0_meta_field "$meta_path" "outer_driver")"
  case "$expected_label" in
    currentsrc.proof.bootstrap)
      [ "$meta_outer_driver" != "" ] || return 1
      [ -x "$meta_outer_driver" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
      ;;
    stage2.proof)
      [ "$meta_outer_driver" != "" ] || return 1
      [ -x "$meta_outer_driver" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_mode")" = "cheng" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_bundle")" != "" ] || return 1
      [ "$(strict_stage0_meta_field "$meta_path" "sidecar_compiler")" != "" ] || return 1
      ;;
    *)
      [ "$meta_outer_driver" = "$driver_path" ] || return 1
      ;;
  esac
  [ "$(strict_stage0_meta_field "$meta_path" "driver_input")" = "$(strict_stage0_expected_input "$driver_path")" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "stage1_ownership_fixed_0_effective")" = "0" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "stage1_ownership_fixed_0_default")" = "0" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "generic_mode")" = "dict" ] || return 1
  [ "$(strict_stage0_meta_field "$meta_path" "generic_lowering")" = "mir_dict" ] || return 1
  strict_stage0_direct_export_surface_ok "$driver_path" || return 1
  return 0
}

strict_stage0_sources_newer_than() {
  out="$1"
  [ -e "$out" ] || return 0
  search_dirs=""
  for d in src/backend src/stage1 src/std src/core src/system; do
    if [ -d "$d" ]; then
      search_dirs="$search_dirs $d"
    fi
  done
  if [ "$search_dirs" = "" ]; then
    return 1
  fi
  # shellcheck disable=SC2086
  find $search_dirs -type f \( -name '*.cheng' -o -name '*.c' -o -name '*.h' \) \
    -newer "$out" -print -quit | grep -q .
}

strict_stage0_current_enough() {
  driver_path="$1"
  strict_stage0_published_surface "$driver_path" || return 0
  [ -x "$driver_path" ] || return 1
  if strict_stage0_sources_newer_than "$driver_path"; then
    return 1
  fi
  return 0
}

repo_stable_exec_ok() {
  path="$1"
  [ "$path" != "" ] || return 1
  [ -x "$path" ] || return 1
  case "$path" in
    "$root"/*) ;;
    *)
      return 1
      ;;
  esac
  case "$path" in
    "$root"/dist/releases/current/*)
      return 1
      ;;
  esac
  return 0
}

live_sidecar_target() {
  if [ "$target" != "" ]; then
    printf '%s\n' "$target"
    return 0
  fi
  detect_host_target
}

live_sidecar_bundle() {
  live_target="$(live_sidecar_target)"
  [ "$live_target" != "" ] || {
    printf '\n'
    return 0
  }
  stable_bundle="$root/chengcache/backend_driver_sidecar/backend_driver_uir_sidecar.$live_target.bundle"
  current_bundle="${stable_bundle}.current"
  if [ -s "$stable_bundle" ]; then
    printf '%s\n' "$stable_bundle"
    return 0
  fi
  if [ -s "$current_bundle" ]; then
    printf '%s\n' "$current_bundle"
    return 0
  fi
  printf '\n'
}

live_sidecar_compiler() {
  live_target="$(live_sidecar_target)"
  [ "$live_target" != "" ] || {
    printf '\n'
    return 0
  }
  compiler="$root/chengcache/backend_driver_sidecar/backend_driver_currentsrc_sidecar_wrapper.$live_target.sh"
  if repo_stable_exec_ok "$compiler"; then
    printf '%s\n' "$compiler"
    return 0
  fi
  printf '\n'
}

live_sidecar_driver() {
  live_target="$(live_sidecar_target)"
  [ "$live_target" != "" ] || {
    printf '\n'
    return 0
  }
  driver="$root/chengcache/backend_driver_sidecar/backend_driver_sidecar_outer.$live_target"
  if repo_stable_exec_ok "$driver"; then
    printf '%s\n' "$driver"
    return 0
  fi
  printf '\n'
}

live_sidecar_real_driver() {
  for raw in "${BACKEND_UIR_SIDECAR_REAL_DRIVER:-}" "${TOOLING_BUILD_GLOBAL_CURRENTSOURCE_REAL_DRIVER:-}"; do
    [ "$raw" != "" ] || continue
    case "$raw" in
      /*)
        path="$raw"
        ;;
      *)
        path="$root/$raw"
        ;;
    esac
    if ! repo_stable_exec_ok "$path"; then
      continue
    fi
    if ! strict_stage0_direct_export_surface_ok "$path"; then
      continue
    fi
    printf '%s\n' "$path"
    return 0
  done
  printf '\n'
}

live_sidecar_child_mode() {
  compiler="$(live_sidecar_compiler)"
  [ "$compiler" != "" ] || {
    printf '\n'
    return 0
  }
  meta_mode="$(strict_stage0_meta_field "${compiler}.meta" "sidecar_child_mode")"
  case "$meta_mode" in
    cli|outer_cli)
      printf '%s\n' "$meta_mode"
      return 0
      ;;
  esac
  printf '\n'
}

live_sidecar_outer_companion() {
  child_mode="$(live_sidecar_child_mode)"
  compiler="$(live_sidecar_compiler)"
  case "$child_mode" in
    cli)
      printf '\n'
      return 0
      ;;
    outer_cli)
      outer="$(strict_stage0_meta_field "${compiler}.meta" "sidecar_outer_companion")"
      if repo_stable_exec_ok "$outer"; then
        printf '%s\n' "$outer"
        return 0
      fi
      printf '\n'
      return 0
      ;;
  esac
  printf '\n'
}

live_sidecar_contract_ok() {
  live_bundle="$(live_sidecar_bundle)"
  live_compiler="$(live_sidecar_compiler)"
  live_driver="$(live_sidecar_driver)"
  live_real_driver_path="$(live_sidecar_real_driver)"
  live_child_mode_path="$(live_sidecar_child_mode)"
  [ "$live_bundle" != "" ] && [ -s "$live_bundle" ] || return 1
  repo_stable_exec_ok "$live_compiler" || return 1
  repo_stable_exec_ok "$live_driver" || return 1
  repo_stable_exec_ok "$live_real_driver_path" || return 1
  case "$live_child_mode_path" in
    cli)
      return 0
      ;;
    outer_cli)
      live_outer="$(live_sidecar_outer_companion)"
      repo_stable_exec_ok "$live_outer" || return 1
      return 0
      ;;
  esac
  return 1
}

field=""
root="$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)"
target=""
while [ "${1:-}" != "" ]; do
  case "$1" in
    --help|-h)
      usage
      exit 0
      ;;
    --field:*)
      field="${1#--field:}"
      ;;
    --root:*)
      root="${1#--root:}"
      ;;
    --target:*)
      target="${1#--target:}"
      ;;
    *)
      echo "[resolve_backend_sidecar_defaults] unknown arg: $1" 1>&2
      usage
      exit 2
      ;;
  esac
  shift || true
done

case "$field" in
  mode|bundle|compiler|driver|real_driver|child_mode|outer_companion)
    ;;
  *)
    echo "[resolve_backend_sidecar_defaults] missing or invalid --field" 1>&2
    usage
    exit 2
    ;;
esac

if [ "$target" = "" ]; then
  target="$(detect_host_target)"
fi

snapshot="$root/artifacts/backend_sidecar_cheng_fresh/verify_backend_sidecar_cheng_fresh.snapshot.env"
use_live_contract=0
if [ -f "$snapshot" ]; then
  snapshot_status="$(sed -n 's/^backend_sidecar_cheng_fresh_status=//p' "$snapshot" | head -n 1)"
  snapshot_mode="$(sed -n 's/^backend_sidecar_cheng_fresh_mode=//p' "$snapshot" | head -n 1)"
  snapshot_compiler="$(sed -n 's/^backend_sidecar_cheng_fresh_compiler=//p' "$snapshot" | head -n 1)"
  snapshot_driver="$(sed -n 's/^backend_sidecar_cheng_fresh_driver=//p' "$snapshot" | head -n 1)"
  snapshot_real_driver="$(sed -n 's/^backend_sidecar_cheng_fresh_real_driver=//p' "$snapshot" | head -n 1)"
  snapshot_bundle="$(sed -n 's/^backend_sidecar_cheng_fresh_bundle=//p' "$snapshot" | head -n 1)"
  snapshot_child_mode="$(sed -n 's/^backend_sidecar_cheng_fresh_child_mode=//p' "$snapshot" | head -n 1)"
  snapshot_outer_companion="$(sed -n 's/^backend_sidecar_cheng_fresh_outer_companion=//p' "$snapshot" | head -n 1)"

  if [ "$snapshot_status" = "ok" ] &&
     [ "$snapshot_mode" = "cheng" ] &&
     repo_stable_exec_ok "$snapshot_compiler" &&
     repo_stable_exec_ok "$snapshot_driver" &&
     repo_stable_exec_ok "$snapshot_real_driver" &&
     strict_stage0_published_surface "$snapshot_real_driver" &&
     strict_stage0_meta_ok "$snapshot_real_driver" &&
     strict_stage0_current_enough "$snapshot_real_driver" &&
     [ "$snapshot_bundle" != "" ] &&
     [ -s "$snapshot_bundle" ]; then
    case "$snapshot_child_mode" in
      cli)
        mode="cheng"
        bundle="$snapshot_bundle"
        compiler="$snapshot_compiler"
        driver="$snapshot_driver"
        real_driver="$snapshot_real_driver"
        child_mode="$snapshot_child_mode"
        outer_companion=""
        ;;
      outer_cli)
        if repo_stable_exec_ok "$snapshot_outer_companion"; then
          mode="cheng"
          bundle="$snapshot_bundle"
          compiler="$snapshot_compiler"
          driver="$snapshot_driver"
          real_driver="$snapshot_real_driver"
          child_mode="$snapshot_child_mode"
          outer_companion="$snapshot_outer_companion"
        else
          use_live_contract=1
        fi
        ;;
      *)
        use_live_contract=1
        ;;
    esac
  else
    use_live_contract=1
  fi
else
  use_live_contract=1
fi

if [ "$use_live_contract" = "1" ]; then
  if ! live_sidecar_contract_ok; then
    echo "[resolve_backend_sidecar_defaults] missing strict fresh sidecar contract and no validated live cache contract" 1>&2
    exit 1
  fi
  mode="cheng"
  bundle="$(live_sidecar_bundle)"
  compiler="$(live_sidecar_compiler)"
  driver="$(live_sidecar_driver)"
  real_driver="$(live_sidecar_real_driver)"
  child_mode="$(live_sidecar_child_mode)"
  outer_companion="$(live_sidecar_outer_companion)"
fi

case "$field" in
  mode)
    printf '%s\n' "$mode"
    ;;
  bundle)
    printf '%s\n' "$bundle"
    ;;
  compiler)
    printf '%s\n' "$compiler"
    ;;
  driver)
    printf '%s\n' "$driver"
    ;;
  real_driver)
    printf '%s\n' "$real_driver"
    ;;
  child_mode)
    printf '%s\n' "$child_mode"
    ;;
  outer_companion)
    printf '%s\n' "$outer_companion"
    ;;
esac
